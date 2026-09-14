// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_SIDEBAR_ITEM_H_
#define CHROME_BROWSER_AVORA_AVORA_SIDEBAR_ITEM_H_

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/prefs/pref_change_registrar.h"

class PrefService;
class PrefRegistrySimple;

namespace avora {

// The three sidebar sections.  All of them are Space-scoped: switching Spaces
// replaces the entire sidebar contents.
enum class SidebarItemType {
  kFavorite = 0,
  kPinned = 1,
  kToday = 2,

  // The last session state of a Favorite's tab.  Stored as a separate record
  // from the Favorite itself so the two URLs stay independent: the Favorite
  // owns the destination the user chose (github.com) while this record holds
  // wherever its tab was left (github.com/my-project/issues/42).
  //
  // The record's |id| is the id of the Favorite it backs, which is what lets a
  // restored tab be reconnected to its Favorite without guessing from history.
  kFavoriteTabState = 3,

  // Durable runtime state for a logical tab, keyed by the tab's Avora GUID.
  // |id| is the GUID; |favorite_id| links a Favorite-backed tab when set.
  kTabState = 4,
};

// A single row in a Space's sidebar.  Favorites, pinned tabs, and today tabs
// are all the same record differing only by |type|, which makes "pin this
// today tab" and "move to another Space" single-field updates.
struct SidebarItem {
  std::string id;

  // Owning Space.  Empty is reserved for a future "global" item that appears
  // in every Space; nothing produces that today.
  std::string space_id;

  std::string url;
  std::string title;

  // When |type| is kTabState and this tab backs a Favorite, holds the
  // Favorite's id.  Empty for ordinary daily tabs.
  std::string favorite_id;

  SidebarItemType type = SidebarItemType::kToday;

  // Sort position within (space_id, type).
  int order = 0;

  base::Time created_at;
  base::Time last_accessed_at;

  bool is_global() const { return space_id.empty(); }

  base::DictValue ToDict() const;
  static SidebarItem FromDict(const base::DictValue& dict);
};

// Persists every Space's sidebar contents in a single pref.
//
// Like SpaceManager, instances are cheap and self-syncing: each one watches the
// backing pref, so several components can own their own store and still see
// each other's writes.
class SidebarItemStore {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnSidebarItemsChanged() {}
  };

  static constexpr char kItemsPref[] = "avora.sidebar_items";

  explicit SidebarItemStore(PrefService* pref_service);
  ~SidebarItemStore();

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Every item, unordered.
  std::vector<SidebarItem> GetAllItems() const;

  // Items for one Space and section, sorted by |order|.  Global items (empty
  // space_id) are included.
  std::vector<SidebarItem> GetItems(const std::string& space_id,
                                    SidebarItemType type) const;

  const SidebarItem* GetItemById(const std::string& id) const;

  // Appends an item to the end of its section.  Returns the new item's id.
  std::string AddItem(const std::string& space_id,
                      SidebarItemType type,
                      const std::string& url,
                      const std::string& title);

  void RemoveItem(const std::string& id);

  // Removes every item belonging to a Space (used when a Space is deleted).
  void RemoveItemsForSpace(const std::string& space_id);

  // Reassigns an item to a different Space without touching the live page.
  void MoveItemToSpace(const std::string& id,
                       const std::string& destination_space_id);

  // Changes an item's section, e.g. promoting a today tab to pinned.
  void SetItemType(const std::string& id, SidebarItemType type);

  void SetItemTitle(const std::string& id, const std::string& title);

  // Reorders an item within its (space, type) section.
  void ReorderItem(const std::string& id, int new_order);

  // Assigns |order| by position for every id listed, in a single write.
  // Unknown ids are ignored; items not listed keep their current order.
  void ReorderItems(const std::vector<std::string>& ordered_ids);

  // Replaces every item of |type| with |items| in a single write, leaving
  // other types untouched.  Entries with an empty id get one generated.
  //
  // |notify| exists because the live tab list resyncs on every tab strip
  // change; firing observers that often would rebuild unrelated sidebar
  // sections for no reason.
  void ReplaceItemsOfType(SidebarItemType type,
                          const std::vector<SidebarItem>& items,
                          bool notify = true);

  // Records that the item was just visited, for expiry bookkeeping.
  void TouchItem(const std::string& id);

  // Deletes today items last accessed longer than |max_age| ago, except those
  // whose (space_id, url) appear in |protected_tabs|.  A zero or negative
  // |max_age| disables expiry and does nothing.  Returns the count removed.
  int SweepExpiredTodayItems(
      base::TimeDelta max_age,
      const std::set<std::pair<std::string, std::string>>& protected_tabs = {});

 private:
  void Save(const std::vector<SidebarItem>& items);
  void RefreshCacheIfNeeded() const;
  void NotifyChanged();

  // Fired when another SidebarItemStore instance writes the backing pref.
  void OnItemsPrefChanged();

  // Next order value for the end of a section.
  int NextOrderFor(const std::vector<SidebarItem>& items,
                   const std::string& space_id,
                   SidebarItemType type) const;

  raw_ptr<PrefService> pref_service_;
  bool pref_available_ = false;

  // Set while this instance is writing, to suppress duplicate notification.
  bool writing_ = false;

  mutable std::vector<SidebarItem> cached_;
  mutable bool cache_dirty_ = true;

  PrefChangeRegistrar pref_registrar_;

  base::ObserverList<Observer> observers_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_SIDEBAR_ITEM_H_
