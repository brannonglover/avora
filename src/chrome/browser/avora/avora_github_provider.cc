// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_github_provider.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "chrome/browser/avora/avora_live_folder_credentials.h"
#include "net/base/load_flags.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace avora {

namespace {

constexpr char kSearchEndpoint[] = "https://api.github.com/search/issues";
constexpr char kUserEndpoint[] = "https://api.github.com/user";

constexpr char kApiVersionHeader[] = "X-GitHub-Api-Version";
constexpr char kApiVersion[] = "2022-11-28";
constexpr char kAcceptValue[] = "application/vnd.github+json";

// GitHub rejects requests without a User-Agent.
constexpr char kUserAgent[] = "Avora-Browser";

constexpr char kRepositoryUrlPrefix[] = "https://api.github.com/repos/";

// Search caps out at 100; 50 is well past what fits in a sidebar and keeps the
// response small enough to parse on the UI thread.
constexpr int kResultsPerQuery = 50;

constexpr int kMaxResponseBytes = 1024 * 1024;  // 1 MB

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("avora_github_live_folder", R"(
        semantics {
          sender: "Avora GitHub Live Folder"
          description:
            "Fetches the pull requests relevant to the GitHub account the "
            "user connected to a Space, so the sidebar can show them as a "
            "self-updating folder."
          trigger:
            "Periodically while a Space with a connected GitHub Live Folder "
            "is active, and when the user manually refreshes the folder."
          data:
            "The user's GitHub personal access token, sent as a bearer "
            "credential, and the saved folder query."
          destination: OTHER
          destination_other: "GitHub's REST API at api.github.com."
        }
        policy {
          cookies_allowed: NO
          setting:
            "Disabled by default.  Active only for Spaces where the user has "
            "explicitly connected a GitHub account, and stops when the "
            "account is disconnected."
        })");

// Qualifiers shared by every sub-query of a fan-out.
std::string BaseQualifiers(const LiveFolderQuery& query) {
  std::vector<std::string> parts = {"is:pr", "is:open", "archived:false"};
  for (const std::string& repo : query.repositories) {
    if (!repo.empty()) {
      parts.push_back(base::StrCat({"repo:", repo}));
    }
  }
  return base::JoinString(parts, " ");
}

std::string ExtractRepoFromApiUrl(const std::string& repository_url) {
  if (!base::StartsWith(repository_url, kRepositoryUrlPrefix)) {
    return std::string();
  }
  return repository_url.substr(std::size(kRepositoryUrlPrefix) - 1);
}

// GitHub's numeric ids arrive as JSON numbers, which base::Value stores as
// doubles once they exceed int range.  Prefer the string node_id and only fall
// back to formatting the number.
std::string ExtractExternalId(const base::DictValue& dict) {
  if (const std::string* node_id = dict.FindString("node_id");
      node_id && !node_id->empty()) {
    return *node_id;
  }
  if (std::optional<double> id = dict.FindDouble("id")) {
    return base::NumberToString(static_cast<int64_t>(*id));
  }
  return std::string();
}

LiveFolderItemStatus StatusForSearchItem(const base::DictValue& dict) {
  if (const base::DictValue* pr = dict.FindDict("pull_request")) {
    if (const base::Value* merged = pr->Find("merged_at");
        merged && merged->is_string()) {
      return LiveFolderItemStatus::kMerged;
    }
  }
  if (const std::string* state = dict.FindString("state");
      state && *state == "closed") {
    return LiveFolderItemStatus::kClosed;
  }
  if (dict.FindBool("draft").value_or(false)) {
    return LiveFolderItemStatus::kDraft;
  }
  return LiveFolderItemStatus::kOpen;
}

int ResponseCodeOf(const network::SimpleURLLoader* loader) {
  if (!loader || !loader->ResponseInfo() || !loader->ResponseInfo()->headers) {
    return 0;
  }
  return loader->ResponseInfo()->headers->response_code();
}

// 403 means both "rate limited" and "forbidden" on GitHub; the remaining-quota
// header is what tells them apart.
bool IsRateLimitResponse(const network::SimpleURLLoader* loader) {
  if (!loader || !loader->ResponseInfo() || !loader->ResponseInfo()->headers) {
    return false;
  }
  const net::HttpResponseHeaders* headers =
      loader->ResponseInfo()->headers.get();
  const std::optional<std::string> remaining =
      headers->GetNormalizedHeader("x-ratelimit-remaining");
  if (!remaining) {
    return false;
  }
  int value = 0;
  return base::StringToInt(*remaining, &value) && value <= 0;
}

std::unique_ptr<network::ResourceRequest> MakeRequest(
    const GURL& url,
    const std::string& token) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "GET";
  // The bearer token is the only credential; sending cookies as well would
  // attach the browser's github.com session to an API call that ignores it.
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_DISABLE_CACHE;
  request->headers.SetHeader("Accept", kAcceptValue);
  request->headers.SetHeader(kApiVersionHeader, kApiVersion);
  request->headers.SetHeader("User-Agent", kUserAgent);
  request->headers.SetHeader("Authorization", base::StrCat({"Bearer ", token}));
  return request;
}

}  // namespace

GitHubLiveFolderProvider::GitHubLiveFolderProvider(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    LiveFolderCredentialStore* credential_store,
    const std::string& space_id)
    : url_loader_factory_(std::move(url_loader_factory)),
      credential_store_(credential_store),
      space_id_(space_id) {}

GitHubLiveFolderProvider::~GitHubLiveFolderProvider() = default;

// ── Query construction ──────────────────────────────────────────────────────

// static
std::vector<std::string> GitHubLiveFolderProvider::BuildSearchQueriesForTesting(
    const LiveFolderQuery& query) {
  LiveFolderQuery effective = query;
  if (effective.HasNoInvolvementFilter()) {
    const LiveFolderQuery defaults = LiveFolderQuery::DefaultForGitHub();
    effective.created_by_me = defaults.created_by_me;
    effective.assigned_to_me = defaults.assigned_to_me;
    effective.review_requested = defaults.review_requested;
    effective.mentioned = defaults.mentioned;
    effective.team_review_requested = defaults.team_review_requested;
  }

  const std::string base_qualifiers = BaseQualifiers(effective);
  std::vector<std::string> involvement;

  // involves: already means author OR assignee OR mentions OR commenter, so
  // collapsing those three into it saves two round trips whenever the user has
  // not narrowed the default set.
  if (effective.created_by_me && effective.assigned_to_me &&
      effective.mentioned) {
    involvement.push_back("involves:@me");
  } else {
    if (effective.created_by_me) {
      involvement.push_back("author:@me");
    }
    if (effective.assigned_to_me) {
      involvement.push_back("assignee:@me");
    }
    if (effective.mentioned) {
      involvement.push_back("mentions:@me");
    }
  }

  // user-review-requested: covers requests made to the account directly and
  // to teams it belongs to, so it supersedes review-requested: entirely.
  if (effective.team_review_requested) {
    involvement.push_back("user-review-requested:@me");
  } else if (effective.review_requested) {
    involvement.push_back("review-requested:@me");
  }

  std::vector<std::string> queries;
  queries.reserve(involvement.size());
  for (const std::string& clause : involvement) {
    queries.push_back(base::StrCat({base_qualifiers, " ", clause}));
  }
  return queries;
}

// ── Response parsing ────────────────────────────────────────────────────────

// static
std::vector<LiveFolderItem>
GitHubLiveFolderProvider::ParseSearchResponseForTesting(
    const std::string& body) {
  std::vector<LiveFolderItem> items;

  std::optional<base::Value> parsed =
      base::JSONReader::Read(body, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return items;
  }
  const base::ListValue* entries = parsed->GetDict().FindList("items");
  if (!entries) {
    return items;
  }

  for (const base::Value& entry : *entries) {
    if (!entry.is_dict()) {
      continue;
    }
    const base::DictValue& dict = entry.GetDict();

    // Search returns issues and PRs together; the pull_request member is what
    // marks the PRs, and a Live Folder of issues is a different feature.
    if (!dict.contains("pull_request")) {
      continue;
    }

    LiveFolderItem item;
    item.external_id = ExtractExternalId(dict);
    if (item.external_id.empty()) {
      continue;
    }
    if (const std::string* title = dict.FindString("title")) {
      item.title = *title;
    }
    if (const std::string* url = dict.FindString("html_url")) {
      item.url = *url;
    }
    if (item.url.empty()) {
      continue;
    }

    item.status = StatusForSearchItem(dict);

    if (const base::DictValue* user = dict.FindDict("user")) {
      if (const std::string* login = user->FindString("login")) {
        item.metadata.author = *login;
      }
    }
    if (const std::string* repo_url = dict.FindString("repository_url")) {
      item.metadata.repo = ExtractRepoFromApiUrl(*repo_url);
    }
    if (std::optional<double> number = dict.FindDouble("number")) {
      item.metadata.number = static_cast<int>(*number);
    }
    if (const std::string* updated = dict.FindString("updated_at")) {
      base::Time time;
      if (base::Time::FromUTCString(updated->c_str(), &time)) {
        item.updated_at = time;
      }
    }

    items.push_back(std::move(item));
  }

  return items;
}

// ── LiveFolderProvider ──────────────────────────────────────────────────────

LiveFolderProviderId GitHubLiveFolderProvider::id() const {
  return LiveFolderProviderId::kGitHub;
}

std::string GitHubLiveFolderProvider::display_name() const {
  return "GitHub";
}

std::string GitHubLiveFolderProvider::CurrentToken() const {
  if (!credential_store_) {
    return std::string();
  }
  return credential_store_->GetToken(space_id_, LiveFolderProviderId::kGitHub);
}

bool GitHubLiveFolderProvider::IsConnected() const {
  return !CurrentToken().empty();
}

std::string GitHubLiveFolderProvider::connected_account() const {
  if (!credential_store_) {
    return std::string();
  }
  return credential_store_->GetAccount(space_id_,
                                       LiveFolderProviderId::kGitHub);
}

void GitHubLiveFolderProvider::Connect(const std::string& credential,
                                       ConnectCallback callback) {
  if (credential.empty()) {
    std::move(callback).Run(false, "Enter a personal access token.");
    return;
  }

  // Validate before storing so a mistyped or insufficiently scoped token is
  // reported here rather than as a silently empty folder later.
  validate_loader_ = network::SimpleURLLoader::Create(
      MakeRequest(GURL(kUserEndpoint), credential), kTrafficAnnotation);
  validate_loader_->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&GitHubLiveFolderProvider::OnValidateResponse,
                     weak_factory_.GetWeakPtr(), std::move(callback),
                     credential),
      kMaxResponseBytes);
}

void GitHubLiveFolderProvider::OnValidateResponse(
    ConnectCallback callback,
    const std::string& credential,
    std::optional<std::string> response_body) {
  const int response_code = ResponseCodeOf(validate_loader_.get());
  validate_loader_.reset();

  if (response_code == net::HTTP_UNAUTHORIZED ||
      response_code == net::HTTP_FORBIDDEN) {
    std::move(callback).Run(
        false, "GitHub rejected that token. Check that it hasn't expired.");
    return;
  }
  if (!response_body || response_code != net::HTTP_OK) {
    std::move(callback).Run(false, "Couldn't reach GitHub. Try again.");
    return;
  }

  std::optional<base::Value> parsed =
      base::JSONReader::Read(*response_body, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    std::move(callback).Run(false, "Couldn't read GitHub's response.");
    return;
  }
  const std::string* login = parsed->GetDict().FindString("login");
  if (!login || login->empty()) {
    std::move(callback).Run(false, "That token has no account attached.");
    return;
  }

  if (credential_store_) {
    credential_store_->SetToken(space_id_, LiveFolderProviderId::kGitHub,
                                credential, *login);
  }
  std::move(callback).Run(true, std::string());
}

void GitHubLiveFolderProvider::Disconnect() {
  CancelFetch();
  validate_loader_.reset();
  if (credential_store_) {
    credential_store_->ClearToken(space_id_, LiveFolderProviderId::kGitHub);
  }
}

void GitHubLiveFolderProvider::FetchItems(const LiveFolderQuery& query,
                                          FetchCallback callback) {
  CancelFetch();

  const std::string token = CurrentToken();
  if (token.empty()) {
    std::move(callback).Run(LiveFolderFetchResult::Error(
        LiveFolderFetchStatus::kNotConnected, std::string()));
    return;
  }

  const std::vector<std::string> queries =
      BuildSearchQueriesForTesting(query);
  if (queries.empty()) {
    std::move(callback).Run(LiveFolderFetchResult::Success({}));
    return;
  }

  fetch_callback_ = std::move(callback);
  include_drafts_ = query.include_drafts;
  merged_items_.clear();
  searches_.clear();
  searches_.resize(queries.size());
  outstanding_ = queries.size();

  for (size_t i = 0; i < queries.size(); ++i) {
    const GURL url(base::StrCat(
        {kSearchEndpoint, "?q=",
         base::EscapeQueryParamValue(queries[i], /*use_plus=*/false),
         "&sort=updated&order=desc&per_page=",
         base::NumberToString(kResultsPerQuery)}));

    searches_[i].loader = network::SimpleURLLoader::Create(
        MakeRequest(url, token), kTrafficAnnotation);
    searches_[i].loader->DownloadToString(
        url_loader_factory_.get(),
        base::BindOnce(&GitHubLiveFolderProvider::OnSearchResponse,
                       weak_factory_.GetWeakPtr(), i),
        kMaxResponseBytes);
  }
}

void GitHubLiveFolderProvider::OnSearchResponse(
    size_t index,
    std::optional<std::string> response_body) {
  if (!fetch_callback_ || index >= searches_.size()) {
    return;
  }

  network::SimpleURLLoader* loader = searches_[index].loader.get();
  const int response_code = ResponseCodeOf(loader);

  if (response_code == net::HTTP_UNAUTHORIZED) {
    FailFetch(LiveFolderFetchStatus::kAuthFailed,
              "GitHub rejected the saved token. Reconnect the account.");
    return;
  }
  if (response_code == net::HTTP_FORBIDDEN ||
      response_code == net::HTTP_TOO_MANY_REQUESTS) {
    if (IsRateLimitResponse(loader)) {
      FailFetch(LiveFolderFetchStatus::kRateLimited,
                "GitHub rate limit reached. Pull requests will refresh "
                "shortly.");
    } else {
      FailFetch(LiveFolderFetchStatus::kAuthFailed,
                "The saved token isn't allowed to read these pull requests.");
    }
    return;
  }
  if (!response_body || response_code != net::HTTP_OK) {
    FailFetch(LiveFolderFetchStatus::kNetworkError,
              "Couldn't reach GitHub.");
    return;
  }

  for (LiveFolderItem& item : ParseSearchResponseForTesting(*response_body)) {
    if (!include_drafts_ && item.status == LiveFolderItemStatus::kDraft) {
      continue;
    }
    // A PR matched by two sub-queries (authored by you and awaiting your
    // review) must appear once.  Both copies carry identical data, so the
    // first one wins.
    merged_items_.emplace(item.external_id, std::move(item));
  }

  searches_[index].loader.reset();
  --outstanding_;
  MaybeFinishFetch();
}

void GitHubLiveFolderProvider::MaybeFinishFetch() {
  if (outstanding_ > 0 || !fetch_callback_) {
    return;
  }

  std::vector<LiveFolderItem> items;
  items.reserve(merged_items_.size());
  for (auto& [external_id, item] : merged_items_) {
    items.push_back(std::move(item));
  }
  merged_items_.clear();
  searches_.clear();

  std::move(fetch_callback_)
      .Run(LiveFolderFetchResult::Success(std::move(items)));
}

void GitHubLiveFolderProvider::FailFetch(LiveFolderFetchStatus status,
                                         const std::string& message) {
  searches_.clear();
  outstanding_ = 0;
  merged_items_.clear();
  if (fetch_callback_) {
    std::move(fetch_callback_).Run(LiveFolderFetchResult::Error(status, message));
  }
}

void GitHubLiveFolderProvider::CancelFetch() {
  searches_.clear();
  outstanding_ = 0;
  merged_items_.clear();
  fetch_callback_.Reset();
}

}  // namespace avora
