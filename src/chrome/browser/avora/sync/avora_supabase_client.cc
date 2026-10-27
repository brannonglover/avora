// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_supabase_client.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace avora::sync {

namespace {

// A full snapshot of a few hundred tabs is well under this; auth responses are
// a few kilobytes.
constexpr size_t kMaxResponseBytes = 256 * 1024;
constexpr base::TimeDelta kRequestTimeout = base::Seconds(30);

// Refresh this long before the server would start refusing the token, so a
// request built just before expiry does not arrive just after it.
constexpr base::TimeDelta kRefreshMargin = base::Seconds(60);

constexpr char kJsonContentType[] = "application/json";

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("avora_tab_sync", R"(
        semantics {
          sender: "Avora Tab Sync"
          description:
            "Signs the user in to their Avora account with a one-time code "
            "sent to their email, and uploads the list of this browser's open "
            "tabs to that account so the Avora phone app can show them."
          trigger:
            "Signing in or out in Avora Settings, and a few seconds after "
            "tabs are opened, closed, navigated, retitled or pinned while "
            "signed in."
          data:
            "The email address and one-time code while signing in; the "
            "session's access and refresh tokens; this device's id and name; "
            "and the URL, title, pinned state, Space name and last-active "
            "time of each open http(s) tab."
          destination: OTHER
          destination_other: "Avora's Supabase project (auth and PostgREST)."
        }
        policy {
          cookies_allowed: NO
          setting:
            "Off until the user signs in under Account in Avora Settings; "
            "signing out stops it and removes this device's tabs."
        })");

int ResponseCodeOf(const network::SimpleURLLoader& loader) {
  if (!loader.ResponseInfo() || !loader.ResponseInfo()->headers) {
    return 0;
  }
  return loader.ResponseInfo()->headers->response_code();
}

RequestStatus StatusForResponseCode(int code) {
  if (code >= 200 && code < 300) {
    return RequestStatus::kOk;
  }
  if (code == 401) {
    return RequestStatus::kUnauthorized;
  }
  if (code == 403) {
    return RequestStatus::kForbidden;
  }
  if (code == 429) {
    return RequestStatus::kRateLimited;
  }
  if (code >= 400 && code < 500) {
    return RequestStatus::kRejected;
  }
  return RequestStatus::kNetworkError;
}

std::string ToJson(const base::DictValue& dict) {
  return base::WriteJson(dict).value_or(std::string());
}

void ParseSession(SupabaseClient::SessionCallback callback,
                  RequestStatus status,
                  std::optional<std::string> body) {
  if (status != RequestStatus::kOk) {
    std::move(callback).Run(status, std::nullopt);
    return;
  }
  std::optional<base::DictValue> dict =
      body ? base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC)
           : std::nullopt;
  std::optional<AuthSession> session =
      dict ? AuthSession::FromResponse(*dict, base::Time::Now())
           : std::nullopt;
  std::move(callback).Run(
      session ? RequestStatus::kOk : RequestStatus::kNetworkError,
      std::move(session));
}

void DropBody(SupabaseClient::StatusCallback callback,
              RequestStatus status,
              std::optional<std::string> body) {
  std::move(callback).Run(status);
}

}  // namespace

// static
std::optional<AuthSession> AuthSession::FromResponse(
    const base::DictValue& dict,
    base::Time now) {
  const std::string* access_token = dict.FindString("access_token");
  const std::string* refresh_token = dict.FindString("refresh_token");
  if (!access_token || access_token->empty() || !refresh_token ||
      refresh_token->empty()) {
    return std::nullopt;
  }
  AuthSession session;
  session.access_token = *access_token;
  session.refresh_token = *refresh_token;
  // expires_in is relative, so it holds even when this Mac's clock is off;
  // expires_at (Unix seconds) is the fallback.
  if (std::optional<int> expires_in = dict.FindInt("expires_in")) {
    session.expires_at = now + base::Seconds(*expires_in);
  } else if (std::optional<double> expires_at = dict.FindDouble("expires_at")) {
    session.expires_at = base::Time::FromSecondsSinceUnixEpoch(*expires_at);
  } else {
    // Unknown lifetime: refresh before the first use.
    session.expires_at = now;
  }
  if (const base::DictValue* user = dict.FindDict("user")) {
    if (const std::string* id = user->FindString("id")) {
      session.user_id = *id;
    }
    if (const std::string* email = user->FindString("email")) {
      session.email = *email;
    }
  }
  return session;
}

bool AuthSession::NeedsRefresh(base::Time now) const {
  return now >= expires_at - kRefreshMargin;
}

SupabaseClient::SupabaseClient(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    std::string base_url,
    std::string publishable_key)
    : url_loader_factory_(std::move(url_loader_factory)),
      base_url_(std::move(base_url)),
      publishable_key_(std::move(publishable_key)) {}

SupabaseClient::~SupabaseClient() = default;

void SupabaseClient::RequestEmailCode(std::string_view email,
                                      StatusCallback callback) {
  base::DictValue body;
  body.Set("email", email);
  body.Set("create_user", true);
  Send("POST", "/auth/v1/otp", std::nullopt, ToJson(body), "",
       base::BindOnce(&DropBody, std::move(callback)));
}

void SupabaseClient::VerifyEmailCode(std::string_view email,
                                     std::string_view code,
                                     SessionCallback callback) {
  base::DictValue body;
  body.Set("type", "email");
  body.Set("email", email);
  body.Set("token", code);
  Send("POST", "/auth/v1/verify", std::nullopt, ToJson(body), "",
       base::BindOnce(&ParseSession, std::move(callback)));
}

void SupabaseClient::RefreshSession(std::string_view refresh_token,
                                    SessionCallback callback) {
  base::DictValue body;
  body.Set("refresh_token", refresh_token);
  Send("POST", "/auth/v1/token?grant_type=refresh_token", std::nullopt,
       ToJson(body), "", base::BindOnce(&ParseSession, std::move(callback)));
}

void SupabaseClient::SignOut(std::string_view access_token,
                             StatusCallback callback) {
  Send("POST", "/auth/v1/logout?scope=local", access_token, std::nullopt, "",
       base::BindOnce(&DropBody, std::move(callback)));
}

void SupabaseClient::UpsertDevice(std::string_view access_token,
                                  std::string body,
                                  StatusCallback callback) {
  Send("POST", "/rest/v1/devices?on_conflict=id", access_token,
       std::move(body), "resolution=merge-duplicates,return=minimal",
       base::BindOnce(&DropBody, std::move(callback)));
}

void SupabaseClient::DeleteDevice(std::string_view access_token,
                                  std::string_view device_id,
                                  StatusCallback callback) {
  Send("DELETE",
       base::StrCat({"/rest/v1/devices?id=eq.",
                     base::EscapeQueryParamValue(device_id, /*use_plus=*/false)}),
       access_token, std::nullopt, "return=minimal",
       base::BindOnce(&DropBody, std::move(callback)));
}

void SupabaseClient::Send(std::string_view method,
                          std::string_view path_and_query,
                          std::optional<std::string_view> access_token,
                          std::optional<std::string> json_body,
                          std::string_view prefer,
                          BodyCallback callback) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(base::StrCat({base_url_, path_and_query}));
  request->method = std::string(method);
  // The bearer token is the only credential.
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_DISABLE_CACHE;
  request->headers.SetHeader("apikey", publishable_key_);
  if (access_token) {
    request->headers.SetHeader("Authorization",
                               base::StrCat({"Bearer ", *access_token}));
  }
  if (!prefer.empty()) {
    request->headers.SetHeader("Prefer", prefer);
  }
  request->headers.SetHeader("Accept", kJsonContentType);

  std::unique_ptr<network::SimpleURLLoader> loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  if (json_body) {
    loader->AttachStringForUpload(std::move(*json_body), kJsonContentType);
  }
  // Error bodies are read too: the status code is what callers act on, and
  // without this a 4xx would be indistinguishable from being offline.
  loader->SetAllowHttpErrorResults(true);
  loader->SetTimeoutDuration(kRequestTimeout);

  network::SimpleURLLoader* raw = loader.get();
  loaders_.push_front(std::move(loader));
  raw->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&SupabaseClient::OnComplete, weak_factory_.GetWeakPtr(),
                     loaders_.begin(), std::move(callback)),
      kMaxResponseBytes);
}

void SupabaseClient::OnComplete(LoaderList::iterator it,
                                BodyCallback callback,
                                std::optional<std::string> body) {
  std::unique_ptr<network::SimpleURLLoader> loader = std::move(*it);
  loaders_.erase(it);
  RequestStatus status = RequestStatus::kNetworkError;
  if (loader->NetError() == net::OK || body) {
    status = StatusForResponseCode(ResponseCodeOf(*loader));
  }
  std::move(callback).Run(status, std::move(body));
}

}  // namespace avora::sync
