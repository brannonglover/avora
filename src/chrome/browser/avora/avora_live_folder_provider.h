// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_PROVIDER_H_
#define CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_PROVIDER_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "chrome/browser/avora/avora_live_folder.h"

namespace avora {

// Why a sync produced no items.  Distinguishing these matters in the UI: a
// folder with no matching PRs shows "You're all caught up", a disconnected one
// shows a Connect button, and a failed one keeps the previous contents on
// screen rather than blanking them.
enum class LiveFolderFetchStatus {
  kSuccess = 0,

  // No credential stored for this Space, so nothing was requested.
  kNotConnected = 1,

  // Credential exists but the service rejected it; it likely expired or was
  // revoked.
  kAuthFailed = 2,

  // Transport failure or an unparseable response.  Transient; retry later.
  kNetworkError = 3,

  // The service asked us to back off.  |retry_after| on the result says when.
  kRateLimited = 4,
};

struct LiveFolderFetchResult {
  LiveFolderFetchStatus status = LiveFolderFetchStatus::kSuccess;

  // Populated only when |status| is kSuccess.  This is the complete set the
  // query matched, not a delta: the store replaces the folder's contents with
  // it, which is what makes removals happen automatically.
  std::vector<LiveFolderItem> items;

  // Set when kRateLimited; zero otherwise.
  base::TimeDelta retry_after;

  // Human-readable detail for the error row.  Never contains the credential.
  std::string error_message;

  bool ok() const { return status == LiveFolderFetchStatus::kSuccess; }

  static LiveFolderFetchResult Success(std::vector<LiveFolderItem> items);
  static LiveFolderFetchResult Error(LiveFolderFetchStatus status,
                                     const std::string& message);
};

// Contract every Live Folder backend implements.
//
// The provider owns its items: FetchItems() returns the authoritative full set
// for a query, and the store replaces rather than merges.  That inversion is
// the whole point of a Live Folder -- the user never adds or removes rows, so
// a merged PR disappears on the next sync without anyone deleting it.
//
// Implementations must be safe to destroy with a fetch in flight; doing so
// cancels the request and drops the callback.
class LiveFolderProvider {
 public:
  using FetchCallback = base::OnceCallback<void(LiveFolderFetchResult)>;
  using ConnectCallback =
      base::OnceCallback<void(bool success, const std::string& error)>;

  virtual ~LiveFolderProvider() = default;

  virtual LiveFolderProviderId id() const = 0;

  // Display name for the connect affordance, e.g. "GitHub".
  virtual std::string display_name() const = 0;

  // True when a usable credential is stored for the Space this provider was
  // created for.  Cheap; safe to call during layout.
  virtual bool IsConnected() const = 0;

  // Validates |credential| against the service and stores it on success.  The
  // credential is a provider-defined opaque string (for GitHub, a personal
  // access token).
  //
  // Validation happens before storage so a typo surfaces immediately rather
  // than as an empty folder later.
  virtual void Connect(const std::string& credential,
                       ConnectCallback callback) = 0;

  // Discards the stored credential.  Does not clear cached items; the caller
  // decides whether a disconnected folder keeps showing its last contents.
  virtual void Disconnect() = 0;

  // Login of the connected account, or empty when not connected.  Populated by
  // Connect() and cached, so this does not hit the network.
  virtual std::string connected_account() const = 0;

  // Fetches the full current result set for |query|.  Any in-flight fetch is
  // cancelled first, so the latest call is the one that reports.
  virtual void FetchItems(const LiveFolderQuery& query,
                          FetchCallback callback) = 0;

  // Cancels an in-flight fetch and drops its callback.
  virtual void CancelFetch() = 0;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_PROVIDER_H_
