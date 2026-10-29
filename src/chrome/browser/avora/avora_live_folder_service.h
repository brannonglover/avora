// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_SERVICE_H_
#define CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_SERVICE_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/supports_user_data.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/avora/avora_live_folder_provider.h"
#include "chrome/browser/avora/avora_live_folder_store.h"
#include "chrome/browser/avora/avora_space_manager.h"

class GURL;
class PrefRegistrySimple;
class Profile;

namespace content {
class StoragePartition;
}  // namespace content

namespace avora {

// Drives Live Folder synchronisation for one browser profile.
//
// Owns the provider instances and the refresh timer, and is the only thing that
// writes provider results into LiveFolderStore.  Views observe it for status
// and observe the store for contents; they never talk to a provider directly.
//
// Only Spaces some window is showing sync.  Each window has its own Space,
// independent of the global default, so views tell the service which Spaces
// are on screen (WatchSpace) and every call names its Space explicitly.
// Background Spaces would multiply requests for folders nobody is looking at,
// so a Space refreshes when a window starts showing it and then on the timer.
//
// Attached to the Profile as user data rather than registered as a
// BrowserContextKeyedService: the latter would mean carrying a copy of
// Chromium's service registration file in the Avora overlay purely to add one
// line, and rebasing that file on every Chromium roll.
class LiveFolderService : public base::SupportsUserData::Data {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // A sync started or finished for |folder_id| in |space_id|; call
    // GetSyncState() for the detail.  Contents changes arrive separately via
    // LiveFolderStore.
    virtual void OnLiveFolderSyncStateChanged(const std::string& space_id,
                                              const std::string& folder_id) {}
  };

  // What the UI needs to render beyond the rows themselves.
  struct SyncState {
    bool syncing = false;
    bool connected = false;

    // Result of the most recent completed sync.
    LiveFolderFetchStatus last_status = LiveFolderFetchStatus::kSuccess;
    std::string last_error;

    // Login of the signed-in account, for the folder subtitle.
    std::string account;

    // Organisations whose items the last successful sync could not see until
    // the user signs in to them separately.
    std::vector<LiveFolderProtectedSource> protected_sources;
  };

  // How often a watched Space's folders refresh.  Each sync is up to two
  // github.com searches, made with the same session the user browses with,
  // so this stays far below anything GitHub would throttle.
  static constexpr base::TimeDelta kRefreshInterval = base::Minutes(5);

  // Minimum spacing between syncs of the same folder, so rapid Space switching
  // or repeated manual refreshes cannot hammer the API.
  static constexpr base::TimeDelta kMinSyncSpacing = base::Seconds(30);

  // Live Folders once stored GitHub personal access tokens.  They now use the
  // Space's github.com session instead, so the old pref is registered only to
  // be cleared.
  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  // The instance for |profile|, creating it on first call.  Returns nullptr for
  // a null or off-the-record profile: Live Folders are a persistent feature and
  // have no meaning in an incognito session.
  static LiveFolderService* GetForProfile(Profile* profile);

  explicit LiveFolderService(Profile* profile);
  LiveFolderService(const LiveFolderService&) = delete;
  LiveFolderService& operator=(const LiveFolderService&) = delete;
  ~LiveFolderService() override;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  LiveFolderStore* store() { return store_.get(); }

  SyncState GetSyncState(const std::string& space_id,
                         const std::string& folder_id) const;

  // Where to sign in so |provider|'s folders in |space_id| can fill.  Signing
  // in happens on the provider's own site, in an ordinary tab of the Space.
  GURL GetSignInUrl(const std::string& space_id, LiveFolderProviderId provider);

  // A window started or stopped showing |space_id|.  Counted, so two windows
  // on the same Space keep it watched until both move away.  Starting to watch
  // refreshes the Space's folders that are due.
  void WatchSpace(const std::string& space_id);
  void UnwatchSpace(const std::string& space_id);

  // Syncs |folder_id| now unless it synced within kMinSyncSpacing.
  void RefreshFolder(const std::string& space_id, const std::string& folder_id);

  // Syncs |folder_id| now regardless of spacing, for an explicit user action.
  void ForceRefreshFolder(const std::string& space_id,
                          const std::string& folder_id);

  // Syncs every folder in |space_id| that is due.
  void RefreshSpace(const std::string& space_id);

  // Called when a tab showing |space_id| navigates to |host|.  A visit to a
  // provider's site is how a Live Folder gets discovered: seeing github.com
  // creates the Pull Requests folder.  It is also how signing in is noticed:
  // a signed-out folder, or one missing an organisation's single sign-on,
  // re-checks whenever the user is on the provider's site, so it fills soon
  // after the sign-in completes.  Does nothing when the user removed the
  // folder.
  void OnHostVisited(const std::string& space_id, const std::string& host);

 private:
  // Second-phase setup, run after the instance is reachable via GetForProfile()
  // so the initial sync can safely call back into the service.
  void Initialize();

  // The partition holding |space_id|'s cookies -- the Space's browser identity
  // -- falling back to the default partition when it cannot be resolved.
  content::StoragePartition* GetStoragePartitionForSpace(
      const std::string& space_id);

  // Provider instances are per (Space, provider) because sign-in is
  // Space-scoped: each Space has its own cookie jar.  Keyed by
  // "space_id/provider".
  LiveFolderProvider* GetOrCreateProvider(const std::string& space_id,
                                          LiveFolderProviderId provider);

  void OnFetchComplete(const std::string& space_id,
                       const std::string& folder_id,
                       LiveFolderProviderId provider_id,
                       LiveFolderFetchResult result);

  // Syncs every watched Space; the refresh timer's task.
  void RefreshWatchedSpaces();

  void SyncFolder(const std::string& space_id,
                  const LiveFolder& folder,
                  bool force);

  void NotifySyncStateChanged(const std::string& space_id,
                              const std::string& folder_id);

  raw_ptr<Profile> profile_;

  std::unique_ptr<LiveFolderStore> store_;
  std::unique_ptr<SpaceManager> space_manager_;

  std::map<std::string, std::unique_ptr<LiveFolderProvider>> providers_;

  // Keyed by "space_id/folder_id": folder ids repeat across Spaces (every
  // Space's GitHub folder is "github-pull-requests").
  std::map<std::string, SyncState> sync_states_;
  std::map<std::string, base::TimeTicks> last_sync_started_;

  // Spaces some window is showing, with how many windows show each.
  std::map<std::string, int> watched_spaces_;

  base::RepeatingTimer refresh_timer_;

  base::ObserverList<Observer> observers_;

  base::WeakPtrFactory<LiveFolderService> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_SERVICE_H_
