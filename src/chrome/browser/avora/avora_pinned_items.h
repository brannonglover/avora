// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_PINNED_ITEMS_H_
#define CHROME_BROWSER_AVORA_AVORA_PINNED_ITEMS_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/avora/avora_sidebar_item.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "components/prefs/pref_service.h"
#include "url/gurl.h"

class PrefRegistrySimple;

namespace avora {

struct PinnedItemEntry {
  // The pinned item's persisted navigation target.  This is deliberately
  // independent of whatever URL a live tab materialized from it currently
  // shows; navigating inside that tab never rewrites this.
  std::string url;
  std::string title;

  // Stable identity used to associate a live tab with this pinned item (see
  // SidebarItemType::kPinnedTabState) and to reference it from a
  // PinnedFolder.
  std::string id;
};

// Pinned items belonging to the *active* Space.
//
// A pinned item is a persisted, source-agnostic sidebar destination that
// survives independently of whether any live tab currently represents it:
// closing its tab, or quitting and restarting Avora, leaves the pinned item
// untouched.  It is materialized into a live tab lazily, on activation --
// never eagerly when Avora starts.  This is distinct from a Favorite
// (kFavorite): both are persisted destinations, but pinned items are meant
// for bulk, workspace-style organization (potentially hundreds of items)
// rather than a small curated top-priority set.
//
// Storage lives in SidebarItemStore as items of type kPinned; this class is a
// thin facade over it that implicitly targets whichever Space is active,
// mirroring FavoritesManager (avora_favorites.h) so every existing call-site
// pattern for Space-scoped sidebar content carries over unchanged.
class PinnedItemsManager : public SidebarItemStore::Observer,
                           public SpaceManagerObserver {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnPinnedItemsChanged() = 0;
  };

  // One-time flag: whether tabs natively pinned before this feature existed
  // have been given a backing kPinned item. Set by
  // AvoraSpaceTabFilter::BackfillNativePinnedTabs(); declared here because
  // PinnedItemsManager is the model that migration populates, mirroring
  // FavoritesManager::kFavoritesMigratedPref's ownership of its own
  // migration flag.
  static constexpr char kNativePinnedTabsBackfilledPref[] =
      "avora.native_pinned_tabs_backfilled";

  explicit PinnedItemsManager(PrefService* prefs);
  PinnedItemsManager(const PinnedItemsManager&) = delete;
  PinnedItemsManager& operator=(const PinnedItemsManager&) = delete;
  ~PinnedItemsManager() override;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  std::vector<PinnedItemEntry> GetPinnedItems() const;
  bool IsPinned(const std::string& url) const;

  // The Space these pinned items belong to.
  std::string GetActiveSpaceId() const;

  // Set the active Space for this manager instance (window-local override).
  // When set, ActiveSpaceId() returns this value instead of querying
  // SpaceManager::GetActiveSpace().  Fires OnPinnedItemsChanged.
  void SetWindowActiveSpaceId(const std::string& id);

  // Identity of the pinned item with |url| in the active Space, or empty.
  std::string GetPinnedItemIdForUrl(const std::string& url) const;

  // The persisted navigation target for |id|, or empty if it is gone.
  std::string GetPinnedItemUrlById(const std::string& id) const;

  // The full entry for |id| in the active Space, or nullopt if it is gone.
  // Used by folder rendering (PinnedFoldersManager only stores item ids, not
  // url/title -- the item itself is the single source of truth for those).
  std::optional<PinnedItemEntry> GetPinnedItemById(const std::string& id) const;

  // Returns the new item's id, or empty if |url| is already pinned in the
  // active Space.
  std::string AddPinnedItem(const std::string& url, const std::string& title);

  // Like AddPinnedItem, but inserts at |index| within the active Space's
  // pinned items instead of appending.  Returns the new item's id, or empty
  // if |url| is already pinned.
  std::string InsertPinnedItemAt(int index,
                                 const std::string& url,
                                 const std::string& title);

  void RemovePinnedItem(const std::string& id);

  // What a pinned item's target becomes when the user types |input| as its
  // URL: trimmed and fixed up the way the omnibox fixes up a typed URL, so
  // "example.com" works, with a bare host upgraded to https (a pin is a
  // destination, and the omnibox would upgrade the typed navigation too).
  // Invalid when |input| does not name somewhere a pin can point: empty,
  // unparseable, or a javascript: URL.
  static GURL FixupPinnedItemUrl(std::string_view input);

  // Whether the pinned item |id| can be pointed at what
  // FixupPinnedItemUrl() makes of |input|, and if not, why.  The edit dialog
  // asks this on every keystroke, so Save is never offered for something
  // SetPinnedItemUrl() would refuse.
  enum class UrlEditCheck {
    kOk,
    // |input| fixes up to the URL the item already has.
    kUnchanged,
    // Empty, unparseable, or a javascript: URL.
    kInvalidUrl,
    // Another pinned item in the same Space already has that URL --
    // AddPinnedItem refuses the same duplicate.
    kAlreadyPinned,
    // |id| is gone, or is not a pinned item.
    kNoSuchItem,
  };
  UrlEditCheck CheckPinnedItemUrl(const std::string& id,
                                  std::string_view input) const;

  // Points the pinned item |id| at what FixupPinnedItemUrl() makes of
  // |input|.  The id, Space, order and folder membership are unchanged, so a
  // live tab materializing the item stays linked to it and simply has a new
  // target to return to (it is not navigated here).  The stored title is
  // replaced with the new URL's display form, since the old one named the
  // old page; a name the user gave the pin is kept separately and survives.
  //
  // Returns true when the item now has that URL, including when it already
  // did (nothing is written then).  Returns false, changing nothing, for
  // anything CheckPinnedItemUrl() rejects.
  bool SetPinnedItemUrl(const std::string& id, std::string_view input);

  void MovePinnedItem(int from_index, int to_index);

  // Assigns order by position for every id listed, in the active Space --
  // e.g. PinnedFoldersManager::RemoveFolder() uses this to append a
  // deleted folder's members, in their prior relative order, after the
  // Space's existing top-level items. Unknown ids are ignored; items not
  // listed keep their current order.
  void ReorderAllItems(const std::vector<std::string>& ordered_ids);

  // Reassigns a pinned item to a different Space without touching any live
  // tab materialized from it.
  void MoveItemToSpace(const std::string& id,
                       const std::string& destination_space_id);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // SidebarItemStore::Observer:
  void OnSidebarItemsChanged() override;

  // SpaceManagerObserver:
  void OnActiveSpaceChanged(const std::string& space_id) override;

 private:
  std::string ActiveSpaceId() const;
  std::vector<SidebarItem> ActivePinnedItems() const;

  void NotifyChanged();

  raw_ptr<PrefService> prefs_;
  std::unique_ptr<SpaceManager> space_manager_;
  std::unique_ptr<SidebarItemStore> item_store_;
  base::ObserverList<Observer> observers_;

  // Window-local active Space override.  When non-empty, ActiveSpaceId()
  // returns this instead of querying SpaceManager::GetActiveSpace().
  std::string window_active_space_id_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_PINNED_ITEMS_H_
