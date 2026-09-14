// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_PINNED_FOLDERS_H_
#define CHROME_BROWSER_AVORA_AVORA_PINNED_FOLDERS_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"

class PrefRegistrySimple;

namespace avora {

struct FolderTab {
  std::string url;
  std::string title;
};

struct PinnedFolder {
  std::string name;
  bool expanded = true;
  std::vector<FolderTab> tabs;
};

// Pinned tab folders belonging to the *active* Space.
//
// Each Space owns its own folder list and custom tab titles; switching Spaces
// swaps both wholesale.  Storage lives in space-keyed prefs; this class is a
// thin facade that implicitly targets whichever Space is active, which keeps
// every existing call site working unchanged.
//
// Observers are notified both when folders or titles are edited and when the
// active Space changes, since either one means the visible set is now different.
class PinnedFoldersManager : public SpaceManagerObserver {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnPinnedFoldersChanged() = 0;
  };

  // Pre-Spaces flat storage.  Retained only long enough to migrate its contents
  // into the active Space once; never written to afterwards.
  static constexpr char kPinnedFoldersPref[] = "avora.pinned_folders";
  static constexpr char kPinnedTabTitlesPref[] = "avora.pinned_tab_titles";

  static constexpr char kPinnedFoldersBySpacePref[] =
      "avora.pinned_folders_by_space";
  static constexpr char kPinnedTabTitlesBySpacePref[] =
      "avora.pinned_tab_titles_by_space";
  static constexpr char kPinnedFoldersMigratedPref[] =
      "avora.pinned_folders_migrated";

  explicit PinnedFoldersManager(PrefService* prefs);
  PinnedFoldersManager(const PinnedFoldersManager&) = delete;
  PinnedFoldersManager& operator=(const PinnedFoldersManager&) = delete;
  ~PinnedFoldersManager() override;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  std::vector<PinnedFolder> GetFolders() const;
  void AddFolder(const std::string& name);
  void RemoveFolder(int index);
  void RenameFolder(int index, const std::string& name);
  void AddTabToFolder(int folder_index,
                      const std::string& url,
                      const std::string& title);
  void RemoveTabFromFolder(int folder_index, const std::string& url);
  void MoveTabToFolder(const std::string& url,
                       const std::string& title,
                       int folder_index);
  int GetFolderIndexForTab(const std::string& url) const;
  void SetFolderExpanded(int index, bool expanded);

  // Custom display titles for pinned tabs (keyed by URL).
  std::string GetCustomTabTitle(const std::string& url) const;
  void SetCustomTabTitle(const std::string& url, const std::string& title);
  void ClearCustomTabTitle(const std::string& url);

  // The Space these pinned folders belong to.
  std::string GetActiveSpaceId() const;

  // Set the active Space for this manager instance (window-local override).
  // When set, ActiveSpaceId() returns this value instead of querying
  // SpaceManager::GetActiveSpace().  Fires OnPinnedFoldersChanged.
  void SetWindowActiveSpaceId(const std::string& id);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // SpaceManagerObserver:
  void OnActiveSpaceChanged(const std::string& space_id) override;

 private:
  std::string ActiveSpaceId() const;
  std::vector<PinnedFolder> ActiveFolders() const;

  // Copies any pre-Spaces folders and tab titles into the active Space, once.
  void MigrateLegacyPinnedIfNeeded();

  void SaveFolders(const std::vector<PinnedFolder>& folders);

  // Fired when another PinnedFoldersManager instance writes the backing pref.
  void OnPinnedPrefsChanged();

  void RemoveTabFromAllFolders(const std::string& url);
  void NotifyChanged();

  raw_ptr<PrefService> prefs_;
  std::unique_ptr<SpaceManager> space_manager_;
  PrefChangeRegistrar pref_registrar_;
  base::ObserverList<Observer> observers_;

  // Window-local active Space override.  When non-empty, ActiveSpaceId()
  // returns this instead of querying SpaceManager::GetActiveSpace().
  std::string window_active_space_id_;

  bool pref_available_ = false;
  bool writing_ = false;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_PINNED_FOLDERS_H_
