// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_FAVORITES_H_
#define CHROME_BROWSER_AVORA_AVORA_FAVORITES_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/avora/avora_sidebar_item.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "components/prefs/pref_service.h"

class PrefRegistrySimple;

namespace avora {

struct FavoriteEntry {
  // The Favorite's persisted navigation target.  This is deliberately
  // independent of whatever URL the associated live tab currently shows;
  // navigating inside that tab never rewrites this.
  std::string url;
  std::string title;

  // Stable identity used to associate a live tab with this Favorite.
  std::string id;
};

// Favorites belonging to the *active* Space.
//
// Favorites are Space-scoped sidebar content, so switching Spaces swaps the
// whole set.  Storage lives in SidebarItemStore as items of type kFavorite;
// this class is a thin facade over it that implicitly targets whichever Space
// is active, which keeps every existing call site working unchanged.
//
// Observers are notified both when favorites are edited and when the active
// Space changes, since either one means the visible set is now different.
class FavoritesManager : public SidebarItemStore::Observer,
                         public SpaceManagerObserver {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnFavoritesChanged() = 0;
  };

  // Pre-Spaces flat list.  Retained only long enough to migrate its contents
  // into the active Space once; never written to afterwards.
  static constexpr char kFavoritesPref[] = "avora.favorites";
  static constexpr char kFavoritesMigratedPref[] = "avora.favorites_migrated";

  explicit FavoritesManager(PrefService* prefs);
  FavoritesManager(const FavoritesManager&) = delete;
  FavoritesManager& operator=(const FavoritesManager&) = delete;
  ~FavoritesManager() override;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  std::vector<FavoriteEntry> GetFavorites() const;
  bool IsFavorited(const std::string& url) const;

  // The Space these Favorites belong to.
  std::string GetActiveSpaceId() const;

  // Set the active Space for this manager instance (window-local override).
  // When set, ActiveSpaceId() returns this value instead of querying
  // SpaceManager::GetActiveSpace().  Fires OnFavoritesChanged.
  void SetWindowActiveSpaceId(const std::string& id);

  // Identity of the Favorite with |url| in the active Space, or empty.
  std::string GetFavoriteIdForUrl(const std::string& url) const;

  // The persisted navigation target for |id|, or empty if it is gone.
  std::string GetFavoriteUrlById(const std::string& id) const;
  void AddFavorite(const std::string& url, const std::string& title);
  void InsertFavoriteAt(int index,
                        const std::string& url,
                        const std::string& title);
  void RemoveFavorite(const std::string& url);
  void MoveFavorite(int from_index, int to_index);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // SidebarItemStore::Observer:
  void OnSidebarItemsChanged() override;

  // SpaceManagerObserver:
  void OnActiveSpaceChanged(const std::string& space_id) override;

 private:
  std::string ActiveSpaceId() const;
  std::vector<SidebarItem> ActiveFavorites() const;

  // Copies any pre-Spaces favorites into the active Space, once.
  void MigrateLegacyFavoritesIfNeeded();

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

#endif  // CHROME_BROWSER_AVORA_AVORA_FAVORITES_H_
