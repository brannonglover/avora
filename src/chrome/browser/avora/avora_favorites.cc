// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_favorites.h"

#include <algorithm>

#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"

namespace avora {

FavoritesManager::FavoritesManager(PrefService* prefs) : prefs_(prefs) {
  if (!prefs_) {
    return;
  }

  space_manager_ = std::make_unique<SpaceManager>(prefs_);
  item_store_ = std::make_unique<SidebarItemStore>(prefs_);

  MigrateLegacyFavoritesIfNeeded();

  item_store_->AddObserver(this);
  space_manager_->AddObserver(this);
}

FavoritesManager::~FavoritesManager() {
  if (item_store_) {
    item_store_->RemoveObserver(this);
  }
  if (space_manager_) {
    space_manager_->RemoveObserver(this);
  }
}

// static
void FavoritesManager::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kFavoritesPref);
  registry->RegisterBooleanPref(kFavoritesMigratedPref, false);
}

// ── Space scoping ───────────────────────────────────────────────────────────

std::string FavoritesManager::ActiveSpaceId() const {
  if (!window_active_space_id_.empty()) {
    return window_active_space_id_;
  }
  if (space_manager_) {
    if (const Space* active = space_manager_->GetActiveSpace()) {
      return active->id;
    }
  }
  return std::string();
}

void FavoritesManager::SetWindowActiveSpaceId(const std::string& id) {
  if (id == window_active_space_id_) {
    return;
  }
  window_active_space_id_ = id;
  NotifyChanged();
}

std::vector<SidebarItem> FavoritesManager::ActiveFavorites() const {
  if (!item_store_) {
    return {};
  }
  return item_store_->GetItems(ActiveSpaceId(), SidebarItemType::kFavorite);
}

// ── Migration ───────────────────────────────────────────────────────────────

void FavoritesManager::MigrateLegacyFavoritesIfNeeded() {
  if (!prefs_ || !item_store_) {
    return;
  }
  // Without the flag pref there is no safe way to know whether we already ran,
  // so do nothing rather than risk importing twice.
  if (!prefs_->FindPreference(kFavoritesMigratedPref)) {
    return;
  }
  if (prefs_->GetBoolean(kFavoritesMigratedPref)) {
    return;
  }
  if (!prefs_->FindPreference(kFavoritesPref)) {
    prefs_->SetBoolean(kFavoritesMigratedPref, true);
    return;
  }

  // Pre-Spaces favorites belong to whichever Space is active on upgrade, which
  // on a first run after upgrading is the seeded default Space.
  const std::string space_id = ActiveSpaceId();
  const base::ListValue& legacy = prefs_->GetList(kFavoritesPref);
  for (const auto& val : legacy) {
    if (!val.is_dict()) {
      continue;
    }
    const base::DictValue& dict = val.GetDict();
    const std::string* url = dict.FindString("url");
    if (!url || url->empty()) {
      continue;
    }
    const std::string* title = dict.FindString("title");
    item_store_->AddItem(space_id, SidebarItemType::kFavorite, *url,
                         title ? *title : std::string());
  }

  prefs_->SetBoolean(kFavoritesMigratedPref, true);
}

// ── Queries ─────────────────────────────────────────────────────────────────

std::vector<FavoriteEntry> FavoritesManager::GetFavorites() const {
  std::vector<FavoriteEntry> result;
  for (const auto& item : ActiveFavorites()) {
    result.push_back({item.url, item.title, item.id});
  }
  return result;
}

std::string FavoritesManager::GetActiveSpaceId() const {
  return ActiveSpaceId();
}

std::string FavoritesManager::GetFavoriteIdForUrl(
    const std::string& url) const {
  for (const auto& item : ActiveFavorites()) {
    if (item.url == url) {
      return item.id;
    }
  }
  return std::string();
}

std::string FavoritesManager::GetFavoriteUrlById(const std::string& id) const {
  if (!item_store_) {
    return std::string();
  }
  if (const SidebarItem* item = item_store_->GetItemById(id)) {
    return item->url;
  }
  return std::string();
}

bool FavoritesManager::IsFavorited(const std::string& url) const {
  for (const auto& item : ActiveFavorites()) {
    if (item.url == url) {
      return true;
    }
  }
  return false;
}

// ── Mutations ───────────────────────────────────────────────────────────────

void FavoritesManager::AddFavorite(const std::string& url,
                                   const std::string& title) {
  if (!item_store_ || IsFavorited(url)) {
    return;
  }
  item_store_->AddItem(ActiveSpaceId(), SidebarItemType::kFavorite, url, title);
}

void FavoritesManager::InsertFavoriteAt(int index,
                                        const std::string& url,
                                        const std::string& title) {
  if (!item_store_ || IsFavorited(url)) {
    return;
  }

  const std::string new_id = item_store_->AddItem(
      ActiveSpaceId(), SidebarItemType::kFavorite, url, title);

  // AddItem appends; shuffle the new item into the requested slot.
  std::vector<std::string> ids;
  for (const auto& item : ActiveFavorites()) {
    if (item.id != new_id) {
      ids.push_back(item.id);
    }
  }
  const int clamped = std::clamp(index, 0, static_cast<int>(ids.size()));
  ids.insert(ids.begin() + clamped, new_id);
  item_store_->ReorderItems(ids);
}

void FavoritesManager::RemoveFavorite(const std::string& url) {
  if (!item_store_) {
    return;
  }
  for (const auto& item : ActiveFavorites()) {
    if (item.url == url) {
      item_store_->RemoveItem(item.id);
      return;
    }
  }
}

void FavoritesManager::MoveFavorite(int from_index, int to_index) {
  if (!item_store_) {
    return;
  }

  std::vector<std::string> ids;
  for (const auto& item : ActiveFavorites()) {
    ids.push_back(item.id);
  }

  const int count = static_cast<int>(ids.size());
  if (from_index < 0 || from_index >= count || to_index < 0 ||
      to_index >= count || from_index == to_index) {
    return;
  }

  const std::string moved = ids[from_index];
  ids.erase(ids.begin() + from_index);
  ids.insert(ids.begin() + to_index, moved);
  item_store_->ReorderItems(ids);
}

// ── Observers ───────────────────────────────────────────────────────────────

void FavoritesManager::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void FavoritesManager::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void FavoritesManager::OnSidebarItemsChanged() {
  NotifyChanged();
}

void FavoritesManager::OnActiveSpaceChanged(const std::string& space_id) {
  // When window-local active Space is set, the global pref change is
  // irrelevant for this instance; the owning view drives changes via
  // SetWindowActiveSpaceId() instead.
  if (!window_active_space_id_.empty()) {
    return;
  }
  // A different Space is showing, so the favorites set has changed wholesale.
  NotifyChanged();
}

void FavoritesManager::NotifyChanged() {
  for (auto& observer : observers_) {
    observer.OnFavoritesChanged();
  }
}

}  // namespace avora
