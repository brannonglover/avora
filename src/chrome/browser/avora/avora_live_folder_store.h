// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_STORE_H_
#define CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_STORE_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/function_ref.h"
#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "chrome/browser/avora/avora_live_folder.h"
#include "components/prefs/pref_change_registrar.h"

class PrefService;
class PrefRegistrySimple;

namespace avora {

// Live Folders, stored per Space.
//
// Every call names its Space.  There is deliberately no "active Space"
// shortcut: each window shows its own Space, independently of the global
// default, so a facade that guessed would read and write the wrong Space's
// folders whenever two windows -- or one window and the global pref --
// disagree.  Instances are cheap and self-syncing -- each watches the backing
// pref, so several components can own one and still see each other's writes.
class LiveFolderStore {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // Some Space's folder set changed: contents, expansion, or membership.
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
  ~LiveFolderStore();

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Folders in |space_id|, sorted by order.
  std::vector<LiveFolder> GetFolders(const std::string& space_id) const;

  std::optional<LiveFolder> GetFolderById(const std::string& space_id,
                                          const std::string& folder_id) const;

  // Creates the "Pull Requests" folder in |space_id| if it is missing, and
  // returns its id.  Idempotent, so callers can invoke it on every startup and
  // Space switch without guarding.
  //
  // Creates the folder even if it was previously dismissed, because reaching
  // here means the user asked for it explicitly.  Auto-creation paths should
  // check WasFolderDismissed() first.
  std::string EnsureGitHubPullRequestsFolder(const std::string& space_id);

  bool HasFolder(const std::string& space_id,
                 const std::string& folder_id) const;

  // True when the user removed |folder_id| from |space_id|.
  bool WasFolderDismissed(const std::string& space_id,
                          const std::string& folder_id) const;

  // Removing a folder records it as dismissed.
  void RemoveFolder(const std::string& space_id, const std::string& folder_id);

  // Drops every Live Folder belonging to a Space, for Space deletion.
  void RemoveFoldersForSpace(const std::string& space_id);

  // Installs a provider's authoritative result set.
  //
  // This is a replace, not a merge: items absent from |items| are gone, which
  // is how a merged pull request disappears without anyone deleting it.  The
  // one thing carried across is each surviving item's |seen| flag, so a sync
  // does not re-announce pull requests the user already looked at.
  void ReplaceItems(const std::string& space_id,
                    const std::string& folder_id,
                    const std::vector<LiveFolderItem>& items);

  // Records a completed sync that produced no change worth persisting, so the
  // UI can show an accurate "updated just now".
  void MarkSynced(const std::string& space_id, const std::string& folder_id);

  void SetExpanded(const std::string& space_id,
                   const std::string& folder_id,
                   bool expanded);

  void SetName(const std::string& space_id,
               const std::string& folder_id,
               const std::string& name);

  void SetQuery(const std::string& space_id,
                const std::string& folder_id,
                const LiveFolderQuery& query);

  // Clears the unseen badge, called once the user has actually looked at the
  // rows (folder expanded, or a row activated).
  void MarkAllItemsSeen(const std::string& space_id,
                        const std::string& folder_id);

  void MarkItemSeen(const std::string& space_id,
                    const std::string& folder_id,
                    const std::string& external_id);

 private:
  void SaveFolders(const std::string& space_id,
                   const std::vector<LiveFolder>& folders);

  // Applies |mutation| to the named folder in |space_id| and saves.  Does
  // nothing when the folder is missing.
  void UpdateFolder(const std::string& space_id,
                    const std::string& folder_id,
                    base::FunctionRef<void(LiveFolder&)> mutation);

  // Fired when another LiveFolderStore instance writes the backing pref.
  void OnLiveFoldersPrefChanged();

  void NotifyChanged();

  raw_ptr<PrefService> pref_service_;
  PrefChangeRegistrar pref_registrar_;
  base::ObserverList<Observer> observers_;

  bool pref_available_ = false;
  bool writing_ = false;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_STORE_H_
