// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_SYNC_AVORA_SUPABASE_CLIENT_H_
#define CHROME_BROWSER_AVORA_SYNC_AVORA_SUPABASE_CLIENT_H_

#include <list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/values.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace avora::sync {

// A signed-in Supabase session. The tokens are secrets: they go only into
// request headers and, encrypted, into SessionStore.
struct AuthSession {
  std::string access_token;
  std::string refresh_token;
  // When `access_token` stops being accepted.
  base::Time expires_at;
  std::string user_id;
  std::string email;

  // Parses the session object /auth/v1/verify and /auth/v1/token return.
  // Null if a token is missing.
  static std::optional<AuthSession> FromResponse(const base::DictValue& dict,
                                                 base::Time now);

  // Whether `access_token` should be refreshed before it is used at `now`.
  bool NeedsRefresh(base::Time now) const;
};

// The outcome of one request.
enum class RequestStatus {
  kOk,
  // No usable response: offline, DNS, TLS, timeout, or a 5xx.
  kNetworkError,
  // 401: the access token was not accepted (expired or revoked).
  kUnauthorized,
  // 403. From PostgREST: the token is fine but row-level security refused the
  // write, which for an upsert means the row id belongs to another account.
  // From /auth/v1/verify: the code is wrong or has expired.
  kForbidden,
  // Any other 4xx: the server understood and said no. For a verify that means
  // a wrong or expired code; for a refresh, a revoked session.
  kRejected,
  // 429: too many emails or requests.
  kRateLimited,
};

// The Supabase calls Avora's tab sync makes, on Chromium's network stack from
// the browser process. Requests carry the project's publishable key and, for
// PostgREST, the user's access token; they never send cookies.
//
// Callbacks run on the UI thread. Destroying the client cancels every request
// in flight without running its callback.
class SupabaseClient {
 public:
  using StatusCallback = base::OnceCallback<void(RequestStatus)>;
  using SessionCallback =
      base::OnceCallback<void(RequestStatus, std::optional<AuthSession>)>;

  // `base_url` is the project URL, without a trailing slash.
  SupabaseClient(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      std::string base_url,
      std::string publishable_key);
  SupabaseClient(const SupabaseClient&) = delete;
  SupabaseClient& operator=(const SupabaseClient&) = delete;
  ~SupabaseClient();

  // POST /auth/v1/otp: emails a one-time code (and a magic link) to `email`,
  // creating the account on first use.
  void RequestEmailCode(std::string_view email, StatusCallback callback);

  // POST /auth/v1/verify with type "email": exchanges the code for a session.
  void VerifyEmailCode(std::string_view email,
                       std::string_view code,
                       SessionCallback callback);

  // POST /auth/v1/token?grant_type=refresh_token.
  void RefreshSession(std::string_view refresh_token,
                      SessionCallback callback);

  // POST /auth/v1/logout?scope=local: revokes this session's refresh token.
  void SignOut(std::string_view access_token, StatusCallback callback);

  // POST /rest/v1/devices?on_conflict=id with
  // Prefer: resolution=merge-duplicates,return=minimal.
  void UpsertDevice(std::string_view access_token,
                    std::string body,
                    StatusCallback callback);

  // DELETE /rest/v1/devices?id=eq.{device_id}.
  void DeleteDevice(std::string_view access_token,
                    std::string_view device_id,
                    StatusCallback callback);

  // The shared tables of docs/SYNC.md.
  enum class Table { kSpaces, kItems };

  using RowsCallback =
      base::OnceCallback<void(RequestStatus, std::optional<base::ListValue>)>;
  using BytesCallback =
      base::OnceCallback<void(RequestStatus, std::optional<std::string>)>;

  // GET /rest/v1/{table}?select=*, tombstones included, oldest change first:
  // rows with updated_at >= `since` (every row when null), `limit` at a time
  // starting at `offset`.
  void FetchRows(std::string_view access_token,
                 Table table,
                 std::optional<std::string_view> since,
                 size_t offset,
                 size_t limit,
                 RowsCallback callback);

  // POST /rest/v1/rpc/sync_items (or sync_spaces) with {"changes": changes}:
  // creates or updates each row, writing only the keys each change carries.
  void SyncRows(std::string_view access_token,
                Table table,
                base::ListValue changes,
                StatusCallback callback);

  // POST /storage/v1/object/tab-previews/{path} with x-upsert: true.
  void UploadPreview(std::string_view access_token,
                     std::string_view path,
                     std::string jpeg,
                     StatusCallback callback);

  // GET /storage/v1/object/authenticated/tab-previews/{path}.
  void DownloadPreview(std::string_view access_token,
                       std::string_view path,
                       BytesCallback callback);

  // DELETE /storage/v1/object/tab-previews/{path}.
  void DeletePreview(std::string_view access_token,
                     std::string_view path,
                     StatusCallback callback);

 private:
  using BodyCallback =
      base::OnceCallback<void(RequestStatus, std::optional<std::string>)>;
  using LoaderList = std::list<std::unique_ptr<network::SimpleURLLoader>>;

  struct Request {
    std::string_view method;
    std::string path_and_query;
    std::optional<std::string_view> access_token;
    std::optional<std::string> body;
    std::string_view content_type = "application/json";
    std::string_view accept = "application/json";
    std::string_view prefer;
    bool upsert = false;
    size_t max_response_bytes = 256 * 1024;
  };

  void Send(std::string_view method,
            std::string_view path_and_query,
            std::optional<std::string_view> access_token,
            std::optional<std::string> json_body,
            std::string_view prefer,
            BodyCallback callback);
  void Send(Request request, BodyCallback callback);
  void OnComplete(LoaderList::iterator it,
                  BodyCallback callback,
                  std::optional<std::string> body);

  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  const std::string base_url_;
  const std::string publishable_key_;
  LoaderList loaders_;
  base::WeakPtrFactory<SupabaseClient> weak_factory_{this};
};

}  // namespace avora::sync

#endif  // CHROME_BROWSER_AVORA_SYNC_AVORA_SUPABASE_CLIENT_H_
