// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_STORE_H_
#define CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_STORE_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/function_ref.h"
#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "chrome/browser/avora/avora_live_folder.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "components/prefs/pref_change_registrar.h"

class PrefService;
class PrefRegistrySimple;

namespace avora {

// Live Folders belonging to the *active* Space.
//
// Follows PinnedFoldersManager: storage is a Space-keyed pref and this class is
// a facade that implicitly targets whichever Space is active, so views never
// have to thread a Space id through.  Instances are cheap and self-syncing --
// each watches the backing pref, so several components can own one and still
// see each other's writes.
class LiveFolderStore : public SpaceManagerObserver {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // The visible folder set changed: contents, expansion, or active Space.
    virtual void OnLiveFoldersChanged() {}
  };

  // Dict[space_id -> List[LiveFolder]].
  static constexpr char kLiveFoldersPref[] = "avora.live_folders_by_space";

  // Dict[space_id -> List[folder_id]] of folders the user removed by hand.
  // Auto-creation consults this so a folder the user deliberately deleted does
  // not reappear the next time they open the provider's website.
  static constexpr char kDismissedFoldersPref[] =
      "avora.live_folders_dismissed";

  // Stable id of the auto-created GitHub folder, so it can be found without a
  // lookup by name (which the user can rename).
  static constexpr char kGitHubPullRequestsFolderId[] = "github-pull-requests";

  explicit LiveFolderStore(PrefService* pref_service);
  LiveFolderStore(const LiveFolderStore&) = delete;
  LiveFolderStore& operator=(const LiveFolderStore&) = delete;
  ~LiveFolderStore() override;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Id of the Space these folders belong to.
  std::string active_space_id() const;

  // Folders in the active Space, sorted by order.
  std::vector<LiveFolder> GetFolders() const;

  // Folders in a specific Space, sorted by order.  Use this when the caller
  // has a window-local active Space that may differ from the global one.
  std::vector<LiveFolder> GetFoldersForSpace(
      const std::string& space_id) const;

  std::optional<LiveFolder> GetFolderById(const std::string& folder_id) const;

  // Creates the "Pull Requests" folder for the active Space if it is missing,
  // and returns its id.  Idempotent, so callers can invoke it on every startup
  // and Space switch without guarding.
  //
  // Creates the folder even if it was previously dismissed, because reaching
  // here means the user asked for it explicitly.  Auto-creation paths should
  // check WasFolderDismissed() first.
  std::string EnsureGitHubPullRequestsFolder();

  bool HasFolder(const std::string& folder_id) const;

  // True when the user removed |folder_id| from the active Space.
  bool WasFolderDismissed(const std::string& folder_id) const;

  // Removing a folder records it as dismissed.
  void RemoveFolder(const std::string& folder_id);

  // Drops every Live Folder belonging to a Space, for Space deletion.
  void RemoveFoldersForSpace(const std::string& space_id);

  // Installs a provider's authoritative result set.
  //
  // This is a replace, not a merge: items absent from |items| are gone, which
  // is how a merged pull request disappears without anyone deleting it.  The
  // one thing carried across is each surviving item's |seen| flag, so a sync
  // does not re-announce pull requests the user already looked at.
  void ReplaceItems(const std::string& folder_id,
                    const std::vector<LiveFolderItem>& items);

  // Records a completed sync that produced no change worth persisting, so the
  // UI can show an accurate "updated just now".
  void MarkSynced(const std::string& folder_id);

  void SetExpanded(const std::string& folder_id, bool expanded);

  void SetName(const std::string& folder_id, const std::string& name);

  void SetQuery(const std::string& folder_id, const LiveFolderQuery& query);

  // Clears the unseen badge, called once the user has actually looked at the
  // rows (folder expanded, or a row activated).
  void MarkAllItemsSeen(const std::string& folder_id);

  void MarkItemSeen(const std::string& folder_id,
                    const std::string& external_id);

  // SpaceManagerObserver:
  void OnActiveSpaceChanged(const std::string& space_id) override;

 private:
  std::vector<LiveFolder> FoldersForSpace(const std::string& space_id) const;

  void SaveFolders(const std::string& space_id,
                   const std::vector<LiveFolder>& folders);

  // Applies |mutation| to the named folder in the active Space and saves.
  // Does nothing when the folder is missing.
  void UpdateFolder(const std::string& folder_id,
                    base::FunctionRef<void(LiveFolder&)> mutation);

  // Fired when another LiveFolderStore instance writes the backing pref.
  void OnLiveFoldersPrefChanged();

  void NotifyChanged();

  raw_ptr<PrefService> pref_service_;
  std::unique_ptr<SpaceManager> space_manager_;
  PrefChangeRegistrar pref_registrar_;
  base::ObserverList<Observer> observers_;

  bool pref_available_ = false;
  bool writing_ = false;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_STORE_H_
