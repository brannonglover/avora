// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_github_provider.h"

#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversion_utils.h"
#include "content/public/browser/storage_partition.h"
#include "net/base/isolation_info.h"
#include "net/base/load_flags.h"
#include "net/cookies/cookie_options.h"
#include "net/cookies/cookie_partition_key_collection.h"
#include "net/cookies/site_for_cookies.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace avora {

namespace {

constexpr char kGitHubOrigin[] = "https://github.com";
constexpr char kSearchEndpoint[] = "https://github.com/search";
constexpr char kSignInUrl[] = "https://github.com/login";

// github.com's own signals for the session, readable without a request.
// |logged_in| is "yes" or "no"; |dotcom_user| carries the login.
constexpr char kLoggedInCookie[] = "logged_in";
constexpr char kLoginCookie[] = "dotcom_user";

// Without this the search route renders its HTML page instead of the JSON the
// page is built from.
constexpr char kAcceptValue[] = "application/json";

constexpr int kMaxResponseBytes = 1024 * 1024;  // 1 MB

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("avora_github_live_folder", R"(
        semantics {
          sender: "Avora GitHub Live Folder"
          description:
            "Searches github.com for the open pull requests that involve the "
            "account the Space is signed in to, so the sidebar can show them "
            "as a self-updating folder."
          trigger:
            "Periodically while a Space with a GitHub Live Folder is active "
            "and signed in to github.com, and when the user manually "
            "refreshes the folder."
          data:
            "The Space's github.com session cookies and the saved folder "
            "query."
          destination: OTHER
          destination_other: "github.com's pull request search."
        }
        policy {
          cookies_allowed: YES
          cookies_store: "The Space's storage partition."
          setting:
            "Active only for Spaces with a GitHub Live Folder, which appears "
            "after visiting github.com and can be removed from its context "
            "menu.  Nothing is requested while the Space is signed out of "
            "github.com."
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

// Search titles are HTML: matched terms come wrapped in <em>, and characters
// such as '/' arrive as entities.  Strips the tags and decodes the entities in
// one pass, so "&amp;lt;" correctly becomes "&lt;" rather than "<".
std::string HtmlToPlainText(std::string_view html) {
  std::string out;
  out.reserve(html.size());
  for (size_t i = 0; i < html.size(); ++i) {
    const char c = html[i];
    if (c == '<') {
      const size_t close = html.find('>', i);
      if (close == std::string_view::npos) {
        break;
      }
      i = close;
      continue;
    }
    if (c != '&') {
      out.push_back(c);
      continue;
    }

    const size_t semi = html.find(';', i);
    if (semi == std::string_view::npos || semi - i > 10) {
      out.push_back(c);
      continue;
    }
    const std::string_view entity = html.substr(i + 1, semi - i - 1);

    uint32_t code_point = 0;
    bool decoded = true;
    if (entity == "amp") {
      code_point = '&';
    } else if (entity == "lt") {
      code_point = '<';
    } else if (entity == "gt") {
      code_point = '>';
    } else if (entity == "quot") {
      code_point = '"';
    } else if (entity == "apos") {
      code_point = '\'';
    } else if (entity.size() > 2 && entity[0] == '#' &&
               (entity[1] == 'x' || entity[1] == 'X')) {
      decoded = base::HexStringToUInt(entity.substr(2), &code_point);
    } else if (entity.size() > 1 && entity[0] == '#') {
      decoded = base::StringToUint(entity.substr(1), &code_point);
    } else {
      decoded = false;
    }

    if (!decoded || !base::IsValidCodepoint(code_point)) {
      out.push_back(c);
      continue;
    }
    base::WriteUnicodeCharacter(code_point, &out);
    i = semi;
  }
  return out;
}

LiveFolderItemStatus StatusForSearchItem(const base::DictValue& dict) {
  if (dict.FindBool("merged").value_or(false)) {
    return LiveFolderItemStatus::kMerged;
  }
  const std::string* state = dict.FindString("state");
  if (state && *state == "closed") {
    return LiveFolderItemStatus::kClosed;
  }
  const std::string* reviewable = dict.FindString("reviewable_state");
  if ((state && *state == "draft") || (reviewable && *reviewable == "draft")) {
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

std::unique_ptr<network::ResourceRequest> MakeRequest(const GURL& url) {
  const url::Origin origin = url::Origin::Create(url);

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "GET";
  // The session cookies are the credential.  Treating the request as
  // github.com's own lets the SameSite ones attach, as they would for a
  // search typed into a github.com tab.
  request->credentials_mode = network::mojom::CredentialsMode::kInclude;
  request->site_for_cookies = net::SiteForCookies::FromOrigin(origin);
  request->trusted_params = network::ResourceRequest::TrustedParams();
  request->trusted_params->isolation_info =
      net::IsolationInfo::CreateForInternalRequest(origin);
  request->load_flags = net::LOAD_DISABLE_CACHE;
  request->headers.SetHeader("Accept", kAcceptValue);
  return request;
}

}  // namespace

GitHubLiveFolderProvider::GitHubLiveFolderProvider(
    content::StoragePartition* storage_partition,
    const std::string& space_id)
    : storage_partition_(storage_partition), space_id_(space_id) {}

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
std::optional<GitHubLiveFolderProvider::SearchResponse>
GitHubLiveFolderProvider::ParseSearchResponseForTesting(
    const std::string& body) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(body, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const base::DictValue* route =
      parsed->GetDict().FindDictByDottedPath("payload.blackbirdSearchRoute");
  if (!route) {
    return std::nullopt;
  }

  SearchResponse response;
  response.logged_in = route->FindBool("logged_in").value_or(false);

  if (const base::ListValue* orgs = route->FindList("protected_org_logins")) {
    for (const base::Value& org : *orgs) {
      if (org.is_string() && !org.GetString().empty()) {
        response.protected_orgs.push_back(org.GetString());
      }
    }
  }

  const base::ListValue* entries = route->FindList("results");
  if (!entries) {
    return response;
  }

  for (const base::Value& entry : *entries) {
    if (!entry.is_dict()) {
      continue;
    }
    const base::DictValue& dict = entry.GetDict();

    // The id is the pull request's own, stable across renames.
    const std::string* id = dict.FindString("id");
    if (!id || id->empty()) {
      continue;
    }

    const base::DictValue* repository =
        dict.FindDictByDottedPath("repo.repository");
    const std::string* owner =
        repository ? repository->FindString("owner_login") : nullptr;
    const std::string* name =
        repository ? repository->FindString("name") : nullptr;
    const std::optional<int> number = dict.FindInt("number");
    if (!owner || !name || !number) {
      continue;
    }

    LiveFolderItem item;
    item.external_id = *id;
    item.metadata.repo = base::StrCat({*owner, "/", *name});
    item.metadata.number = *number;
    item.url = base::StrCat({kGitHubOrigin, "/", item.metadata.repo, "/pull/",
                             base::NumberToString(*number)});
    if (const std::string* title = dict.FindString("hl_title")) {
      item.title = HtmlToPlainText(*title);
    }
    if (const std::string* author = dict.FindString("author_name")) {
      item.metadata.author = *author;
    }
    item.status = StatusForSearchItem(dict);

    // Search reports when a PR was opened but not when it last changed, so
    // the folder orders by newest PR first.
    if (const std::string* created = dict.FindString("created")) {
      base::Time time;
      if (base::Time::FromUTCString(created->c_str(), &time)) {
        item.updated_at = time;
      }
    }

    response.items.push_back(std::move(item));
  }

  return response;
}

// ── LiveFolderProvider ──────────────────────────────────────────────────────

LiveFolderProviderId GitHubLiveFolderProvider::id() const {
  return LiveFolderProviderId::kGitHub;
}

std::string GitHubLiveFolderProvider::display_name() const {
  return "GitHub";
}

bool GitHubLiveFolderProvider::IsConnected() const {
  return signed_in_;
}

std::string GitHubLiveFolderProvider::connected_account() const {
  return signed_in_ ? account_ : std::string();
}

GURL GitHubLiveFolderProvider::sign_in_url() const {
  return GURL(kSignInUrl);
}

void GitHubLiveFolderProvider::FetchItems(const LiveFolderQuery& query,
                                          FetchCallback callback) {
  CancelFetch();

  if (!storage_partition_) {
    std::move(callback).Run(LiveFolderFetchResult::Error(
        LiveFolderFetchStatus::kNetworkError, "Couldn't reach GitHub."));
    return;
  }

  std::vector<std::string> queries = BuildSearchQueriesForTesting(query);
  if (queries.empty()) {
    std::move(callback).Run(LiveFolderFetchResult::Success({}));
    return;
  }

  fetch_callback_ = std::move(callback);
  include_drafts_ = query.include_drafts;

  storage_partition_->GetCookieManagerForBrowserProcess()->GetCookieList(
      GURL(kGitHubOrigin), net::CookieOptions::MakeAllInclusive(),
      net::CookiePartitionKeyCollection(),
      base::BindOnce(&GitHubLiveFolderProvider::OnCookiesRead,
                     weak_factory_.GetWeakPtr(), std::move(queries)));
}

void GitHubLiveFolderProvider::OnCookiesRead(
    std::vector<std::string> queries,
    const net::CookieAccessResultList& cookies,
    const net::CookieAccessResultList& excluded_cookies) {
  if (!fetch_callback_) {
    return;
  }

  bool logged_in = false;
  std::string account;
  for (const net::CookieWithAccessResult& entry : cookies) {
    if (entry.cookie.Name() == kLoggedInCookie) {
      logged_in = entry.cookie.Value() == "yes";
    } else if (entry.cookie.Name() == kLoginCookie) {
      account = entry.cookie.Value();
    }
  }

  if (!logged_in) {
    ReportSignedOut();
    return;
  }

  signed_in_ = true;
  account_ = account;
  StartSearches(queries);
}

void GitHubLiveFolderProvider::StartSearches(
    const std::vector<std::string>& queries) {
  merged_items_.clear();
  protected_orgs_.clear();
  searches_.clear();
  searches_.resize(queries.size());
  outstanding_ = queries.size();

  network::SharedURLLoaderFactory* url_loader_factory =
      storage_partition_->GetURLLoaderFactoryForBrowserProcess().get();

  for (size_t i = 0; i < queries.size(); ++i) {
    // Results arrive ten to a page, newest activity first; the first page is
    // what fits in a sidebar anyway.
    const GURL url(base::StrCat(
        {kSearchEndpoint,
         "?q=", base::EscapeQueryParamValue(queries[i], /*use_plus=*/false),
         "&type=pullrequests&s=updated&o=desc"}));

    searches_[i].loader =
        network::SimpleURLLoader::Create(MakeRequest(url), kTrafficAnnotation);
    searches_[i].loader->DownloadToString(
        url_loader_factory,
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

  const int response_code = ResponseCodeOf(searches_[index].loader.get());

  if (response_code == net::HTTP_TOO_MANY_REQUESTS) {
    FailFetch(LiveFolderFetchStatus::kRateLimited,
              "GitHub is limiting searches. Pull requests will refresh "
              "shortly.");
    return;
  }
  if (response_code == net::HTTP_UNAUTHORIZED ||
      response_code == net::HTTP_FORBIDDEN) {
    FailFetch(LiveFolderFetchStatus::kAuthFailed,
              "GitHub didn't allow this search.");
    return;
  }
  if (!response_body || response_code != net::HTTP_OK) {
    FailFetch(LiveFolderFetchStatus::kNetworkError, "Couldn't reach GitHub.");
    return;
  }

  std::optional<SearchResponse> response =
      ParseSearchResponseForTesting(*response_body);
  if (!response) {
    FailFetch(LiveFolderFetchStatus::kNetworkError,
              "Couldn't read GitHub's response.");
    return;
  }

  // The cookies said signed in but github.com disagreed: the session expired
  // or was signed out elsewhere.  Every sub-query would say the same.
  if (!response->logged_in) {
    ReportSignedOut();
    return;
  }

  for (LiveFolderItem& item : response->items) {
    if (!include_drafts_ && item.status == LiveFolderItemStatus::kDraft) {
      continue;
    }
    // A PR matched by two sub-queries (authored by you and awaiting your
    // review) must appear once.  Both copies carry identical data, so the
    // first one wins.
    merged_items_.emplace(item.external_id, std::move(item));
  }
  protected_orgs_.insert(response->protected_orgs.begin(),
                         response->protected_orgs.end());

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

  LiveFolderFetchResult result =
      LiveFolderFetchResult::Success(std::move(items));
  for (const std::string& org : protected_orgs_) {
    // GitHub's own entry point for an organisation's single sign-on; it sends
    // the user through the identity provider and back to github.com.
    result.protected_sources.push_back(
        {org, GURL(base::StrCat(
                  {kGitHubOrigin, "/orgs/", base::EscapePath(org), "/sso"}))});
  }
  protected_orgs_.clear();

  std::move(fetch_callback_).Run(std::move(result));
}

void GitHubLiveFolderProvider::ReportSignedOut() {
  signed_in_ = false;
  account_.clear();
  FailFetch(LiveFolderFetchStatus::kNotConnected, std::string());
}

void GitHubLiveFolderProvider::FailFetch(LiveFolderFetchStatus status,
                                         const std::string& message) {
  searches_.clear();
  outstanding_ = 0;
  merged_items_.clear();
  protected_orgs_.clear();
  if (fetch_callback_) {
    std::move(fetch_callback_)
        .Run(LiveFolderFetchResult::Error(status, message));
  }
}

void GitHubLiveFolderProvider::CancelFetch() {
  // Drops the callbacks of a cookie read or search still in flight, so a
  // superseded fetch can never report into the new one.
  weak_factory_.InvalidateWeakPtrs();
  searches_.clear();
  outstanding_ = 0;
  merged_items_.clear();
  protected_orgs_.clear();
  fetch_callback_.Reset();
}

}  // namespace avora
