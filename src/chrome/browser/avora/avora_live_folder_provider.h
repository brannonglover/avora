// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_PROVIDER_H_
#define CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_PROVIDER_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "chrome/browser/avora/avora_live_folder.h"
#include "url/gurl.h"

namespace avora {

// Why a sync produced no items.  Distinguishing these matters in the UI: a
// folder with no matching PRs shows nothing, a signed-out one shows a sign-in
// prompt, and a failed one keeps the previous contents on screen rather than
// blanking them.
enum class LiveFolderFetchStatus {
  kSuccess = 0,

  // The Space is not signed in to the service, so there is nothing to list.
  kNotConnected = 1,

  // The service refused the request outright -- typically an organisation
  // that blocks the signed-in session from seeing its pull requests.
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

  // Human-readable detail for the error row.
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

  virtual ~LiveFolderProvider() = default;

  virtual LiveFolderProviderId id() const = 0;

  // Display name for the sign-in affordance, e.g. "GitHub".
  virtual std::string display_name() const = 0;

  // Providers have no credentials of their own: they ride on the session the
  // Space already has with the service's website, exactly as a tab would.
  // Signing in or out on the site is therefore the whole connect story, and
  // these report what the most recent fetch observed.

  // True when the last fetch found a signed-in session.  Cheap; safe to call
  // during layout.
  virtual bool IsConnected() const = 0;

  // Login of the signed-in account, or empty when signed out or unknown.
  virtual std::string connected_account() const = 0;

  // Where to send the user to sign in.
  virtual GURL sign_in_url() const = 0;

  // Fetches the full current result set for |query|.  Any in-flight fetch is
  // cancelled first, so the latest call is the one that reports.
  virtual void FetchItems(const LiveFolderQuery& query,
                          FetchCallback callback) = 0;

  // Cancels an in-flight fetch and drops its callback.
  virtual void CancelFetch() = 0;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_PROVIDER_H_
