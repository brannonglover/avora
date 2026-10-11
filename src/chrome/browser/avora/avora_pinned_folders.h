// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_PINNED_FOLDERS_H_
#define CHROME_BROWSER_AVORA_AVORA_PINNED_FOLDERS_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/avora/avora_pinned_items.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"

class PrefRegistrySimple;

namespace avora {

// A persistent organizational grouping of pinned items.
//
// A folder's membership is a list of kPinned item ids (see
// avora_pinned_items.h) -- never a URL, a tab, or a WebContents. A folder and
// its membership exist independently of whether any member is currently
// materialized into a live tab: url/title for display come from the item
// itself (PinnedItemsManager::GetPinnedItemById), never a redundant copy
// stored on the folder.
struct PinnedFolder {
  // Stable identity, assigned once at creation (or, for folders that
  // predate this field, at one-time migration) and never reassigned.
  // Folder-list array position is not identity -- see
  // PinnedFoldersManager's class comment.
  std::string id;

  std::string name;
  bool expanded = true;

  // kPinned item ids, in this folder's display order. An id here is never
  // also referenced by another folder; see MoveItemToFolder().
  std::vector<std::string> ordered_item_ids;

  // Where this folder sits among the Space's *top-level* pinned items: the
  // id of the item it is drawn after.  An empty string draws it before every
  // item, and nullopt -- the default, and what every folder had before
  // folders could be placed at all -- draws it after every item, which is
  // where folders have always gone.
  //
  // An item id rather than a position, so the placement survives pins being
  // added and removed around it: a count would silently shift the folder
  // every time a pin above it appeared or went away.  An anchor naming an
  // item that is gone (unpinned, or filed into a folder) falls back to the
  // default, which is the only place it could safely be drawn anyway.
  std::optional<std::string> after_item_id;
};

// One folder's place in the Space's top-level layout; see
// PinnedFoldersManager::SetFolderPlacements().
struct FolderPlacement {
  std::string folder_id;
  std::optional<std::string> after_item_id;
};

// Pinned folders belonging to the *active* Space.
//
// Each Space owns its own folder list; switching Spaces swaps it wholesale.
// Storage lives in space-keyed prefs; this class is a thin facade that
// implicitly targets whichever Space is active, mirroring PinnedItemsManager
// and FavoritesManager so every existing call-site pattern for Space-scoped
// sidebar content carries over unchanged.
//
// Folders are identified by |PinnedFolder::id|, never by array position:
// removing, reordering, or deleting a folder must never invalidate another
// folder's identity the way an index would.
//
// Observers are notified both when folders are edited and when the active
// Space changes, since either one means the visible set is now different.
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

  // One-time schema migration: assigns a stable |id| to every folder that
  // predates it, and converts membership from {url,title} pairs to kPinned
  // item ids (resolving or creating an item per legacy URL via
  // PinnedItemsManager, exactly as AvoraSpaceTabFilter::
  // BackfillNativePinnedTabs() already does for native pinned tabs).  Runs
  // once across every Space, not just the active one, so a Space the user
  // has not yet visited this session does not sit in the old schema
  // indefinitely.
  static constexpr char kPinnedFolderSchemaMigratedPref[] =
      "avora.pinned_folder_schema_migrated";

  explicit PinnedFoldersManager(PrefService* prefs);
  PinnedFoldersManager(const PinnedFoldersManager&) = delete;
  PinnedFoldersManager& operator=(const PinnedFoldersManager&) = delete;
  ~PinnedFoldersManager() override;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  std::vector<PinnedFolder> GetFolders() const;

  // Returns the new folder's stable id.
  std::string AddFolder(const std::string& name);

  // Removes the folder.  Its member items are NOT deleted, unpinned, or
  // otherwise touched -- they become top-level pinned items, appended after
  // the Space's existing top-level items in their prior relative (in-folder)
  // order.  Folder deletion is organizational, never destructive: no
  // kPinned record and no live tab is ever affected.
  void RemoveFolder(const std::string& folder_id);

  void RenameFolder(const std::string& folder_id, const std::string& name);
  void SetFolderExpanded(const std::string& folder_id, bool expanded);

  // Absolute position within the Space's folder list.
  void ReorderFolder(const std::string& folder_id, int new_index);

  // Rewrites where folders sit among the top-level pinned items: |placements|
  // is the folder list in display order, each entry carrying the item its
  // folder follows (see PinnedFolder::after_item_id).  Folders left out of
  // the list keep their current placement and are appended after the listed
  // ones.
  //
  // Both halves of a folder's placement -- its order among folders and its
  // anchor among items -- move together in one write, because they are one
  // piece of information: the layout the user just arranged.
  void SetFolderPlacements(const std::vector<FolderPlacement>& placements);

  // Moves |pinned_item_id| into |folder_id|, first removing it from
  // whichever folder (if any) currently contains it -- an item belongs to
  // at most one folder at a time.  An empty |folder_id| moves the item back
  // to top-level.  Never touches any live tab materialized from the item;
  // this is purely an organizational change (see
  // avora_pinned_item_materializer.h for the live-tab side of Pinned).
  void MoveItemToFolder(const std::string& pinned_item_id,
                       const std::string& folder_id);

  // Reorders |pinned_item_id| within |folder_id|.  A no-op if the item is
  // not currently a member of that folder.
  void ReorderItemInFolder(const std::string& folder_id,
                          const std::string& pinned_item_id,
                          int new_index);

  // Places |pinned_item_id| at |index| within |folder_id| -- the positional
  // counterpart of MoveItemToFolder(), which can only append.  An empty
  // |folder_id| takes the item out of every folder, back to top-level, where
  // ordering lives in PinnedItemsManager instead.
  //
  // |index| counts positions in the destination folder *after* the item has
  // been taken out of it, so dragging an item downwards inside its own
  // folder needs no caller-side adjustment.  It is a single write, so a drop
  // never parks the item at the end of the folder for a frame before jumping
  // to where it was actually dropped.
  void MoveItemToFolderAt(const std::string& pinned_item_id,
                          const std::string& folder_id,
                          int index);

  // Scrubs |pinned_item_id| from every folder's membership without
  // promoting it anywhere -- for when the item itself is being deleted
  // (unpinned), so no dangling id is ever left behind.  Safe to call
  // whether or not the item is a member of any folder.
  void RemoveItemFromAllFolders(const std::string& pinned_item_id);

  // The id of the folder containing |pinned_item_id|, or empty if it is a
  // top-level item (or not a pinned item at all).
  std::string GetFolderIdForItem(const std::string& pinned_item_id) const;

  // Custom display titles for pinned rows, by storage key.  Unrelated to
  // folder membership: this serves the rename gesture on a row, which has
  // nowhere else to put a name (a natively pinned tab has no backing
  // kPinned item at all, and a pinned item's own title is the page's, not
  // the user's).
  //
  // Prefer the item-keyed pair below.  These take the raw key, which for
  // everything written before renames were keyed by item id is a URL.
  std::string GetCustomTabTitle(const std::string& key) const;
  void SetCustomTabTitle(const std::string& key, const std::string& title);
  void ClearCustomTabTitle(const std::string& key);

  // A rename belongs to the pinned item, never to a URL.  Two pins routinely
  // sit on the same page without being the same pin -- an SSO landing page,
  // a dashboard root, two accounts on one service -- and keying their names
  // by URL made renaming either one rename both.  Item ids cannot collide
  // that way.
  //
  // |fallback_url| covers the two cases an id cannot: a row with no kPinned
  // record behind it (|item_id| empty), which has only its URL to be known
  // by, and names written before this was keyed by id.  Pass the pin's own
  // persisted target, never the live tab's current URL -- the latter is
  // exactly the key that collides.
  std::string GetCustomTitleForItem(const std::string& item_id,
                                    const std::string& fallback_url) const;
  void SetCustomTitleForItem(const std::string& item_id,
                             const std::string& fallback_url,
                             const std::string& title);

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

  // Upgrades every Space's folders to the current schema (id + item ids),
  // once.  See kPinnedFolderSchemaMigratedPref.
  void MigrateFolderSchemaIfNeeded();

  void SaveFolders(const std::vector<PinnedFolder>& folders);

  // Fired when another PinnedFoldersManager instance writes the backing pref.
  void OnPinnedPrefsChanged();

  void NotifyChanged();

  raw_ptr<PrefService> prefs_;
  std::unique_ptr<SpaceManager> space_manager_;

  // Folders organize items; PinnedFoldersManager therefore depends on
  // PinnedItemsManager, never the reverse.  Used to resolve item url/title
  // for legacy-data migration and to re-order promoted items on
  // RemoveFolder().
  std::unique_ptr<PinnedItemsManager> pinned_items_manager_;

  PrefChangeRegistrar pref_registrar_;
  base::ObserverList<Observer> observers_;

  // Window-local active Space override.  When non-empty, ActiveSpaceId()
  // returns this instead of querying SpaceManager::GetActiveSpace().
  std::string window_active_space_id_;

  bool pref_available_ = false;
  bool writing_ = false;
};

// Reassigns a pinned item to |destination_space_id|, dropping the folder
// membership it cannot take with it.
//
// Folders are per-Space, so an item that changes Space cannot stay a member of
// a folder in the Space it left; without the scrub that folder keeps an id
// whose item is gone.  Every "Move to Space" path goes through here -- the tab
// context menu (via AvoraSpaceTabFilter), a pinned row's menu, a folder
// member's menu -- so the two halves can never drift apart.
//
// |source_space_id| is the Space the item is leaving; pass empty when it is
// not known, which skips the scrub rather than guessing at a Space whose
// folders would then be rewritten wrongly.
void MovePinnedItemToSpace(PrefService* prefs,
                           const std::string& item_id,
                           const std::string& source_space_id,
                           const std::string& destination_space_id);

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_PINNED_FOLDERS_H_
