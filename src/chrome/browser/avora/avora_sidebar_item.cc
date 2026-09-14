// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_sidebar_item.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "base/json/values_util.h"
#include "base/uuid.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace avora {

namespace {

bool IsPrefRegistered(PrefService* ps) {
  return ps && ps->FindPreference(SidebarItemStore::kItemsPref);
}

int TypeToInt(SidebarItemType type) {
  return static_cast<int>(type);
}

SidebarItemType IntToType(int value) {
  switch (value) {
    case 0:
      return SidebarItemType::kFavorite;
    case 1:
      return SidebarItemType::kPinned;
    case 2:
      return SidebarItemType::kToday;
    case 3:
      return SidebarItemType::kFavoriteTabState;
    case 4:
      return SidebarItemType::kTabState;
    default:
      return SidebarItemType::kToday;
  }
}

}  // namespace

// ── SidebarItem ─────────────────────────────────────────────────────────────

base::DictValue SidebarItem::ToDict() const {
  return base::DictValue()
      .Set("id", id)
      .Set("space_id", space_id)
      .Set("url", url)
      .Set("title", title)
      .Set("favorite_id", favorite_id)
      .Set("type", TypeToInt(type))
      .Set("order", order)
      .Set("created_at", base::TimeToValue(created_at))
      .Set("last_accessed_at", base::TimeToValue(last_accessed_at));
}

SidebarItem SidebarItem::FromDict(const base::DictValue& dict) {
  SidebarItem item;
  if (const std::string* val = dict.FindString("id")) {
    item.id = *val;
  }
  if (const std::string* val = dict.FindString("space_id")) {
    item.space_id = *val;
  }
  if (const std::string* val = dict.FindString("url")) {
    item.url = *val;
  }
  if (const std::string* val = dict.FindString("title")) {
    item.title = *val;
  }
  if (const std::string* val = dict.FindString("favorite_id")) {
    item.favorite_id = *val;
  }
  if (std::optional<int> val = dict.FindInt("type")) {
    item.type = IntToType(*val);
  }
  if (std::optional<int> val = dict.FindInt("order")) {
    item.order = *val;
  }
  if (const base::Value* val = dict.Find("created_at")) {
    if (std::optional<base::Time> t = base::ValueToTime(*val)) {
      item.created_at = *t;
    }
  }
  if (const base::Value* val = dict.Find("last_accessed_at")) {
    if (std::optional<base::Time> t = base::ValueToTime(*val)) {
      item.last_accessed_at = *t;
    }
  }
  return item;
}

// ── SidebarItemStore ────────────────────────────────────────────────────────

SidebarItemStore::SidebarItemStore(PrefService* pref_service)
    : pref_service_(pref_service),
      pref_available_(IsPrefRegistered(pref_service)) {
  if (!pref_available_) {
    return;
  }
  pref_registrar_.Init(pref_service);
  pref_registrar_.Add(
      kItemsPref,
      base::BindRepeating(&SidebarItemStore::OnItemsPrefChanged,
                          base::Unretained(this)));
}

SidebarItemStore::~SidebarItemStore() = default;

void SidebarItemStore::OnItemsPrefChanged() {
  if (writing_) {
    return;
  }
  cache_dirty_ = true;
  NotifyChanged();
}

// static
void SidebarItemStore::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kItemsPref);
}

void SidebarItemStore::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void SidebarItemStore::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void SidebarItemStore::NotifyChanged() {
  for (auto& observer : observers_) {
    observer.OnSidebarItemsChanged();
  }
}

void SidebarItemStore::RefreshCacheIfNeeded() const {
  if (!cache_dirty_) {
    return;
  }
  if (!pref_available_) {
    cache_dirty_ = false;
    return;
  }
  const base::ListValue& list = pref_service_->GetList(kItemsPref);
  cached_.clear();
  for (const auto& val : list) {
    if (val.is_dict()) {
      cached_.push_back(SidebarItem::FromDict(val.GetDict()));
    }
  }
  cache_dirty_ = false;
}

// ── Queries ─────────────────────────────────────────────────────────────────

std::vector<SidebarItem> SidebarItemStore::GetAllItems() const {
  RefreshCacheIfNeeded();
  return cached_;
}

std::vector<SidebarItem> SidebarItemStore::GetItems(
    const std::string& space_id,
    SidebarItemType type) const {
  RefreshCacheIfNeeded();
  std::vector<SidebarItem> result;
  for (const auto& item : cached_) {
    if (item.type != type) {
      continue;
    }
    if (item.space_id == space_id || item.is_global()) {
      result.push_back(item);
    }
  }
  std::stable_sort(result.begin(), result.end(),
                   [](const SidebarItem& a, const SidebarItem& b) {
                     return a.order < b.order;
                   });
  return result;
}

const SidebarItem* SidebarItemStore::GetItemById(const std::string& id) const {
  RefreshCacheIfNeeded();
  for (const auto& item : cached_) {
    if (item.id == id) {
      return &item;
    }
  }
  return nullptr;
}

int SidebarItemStore::NextOrderFor(const std::vector<SidebarItem>& items,
                                   const std::string& space_id,
                                   SidebarItemType type) const {
  int max_order = -1;
  for (const auto& item : items) {
    if (item.type == type && item.space_id == space_id) {
      max_order = std::max(max_order, item.order);
    }
  }
  return max_order + 1;
}

// ── Mutations ───────────────────────────────────────────────────────────────

std::string SidebarItemStore::AddItem(const std::string& space_id,
                                      SidebarItemType type,
                                      const std::string& url,
                                      const std::string& title) {
  RefreshCacheIfNeeded();
  auto items = cached_;

  const base::Time now = base::Time::Now();

  SidebarItem item;
  item.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  item.space_id = space_id;
  item.url = url;
  item.title = title;
  item.type = type;
  item.order = NextOrderFor(items, space_id, type);
  item.created_at = now;
  item.last_accessed_at = now;

  const std::string new_id = item.id;
  items.push_back(std::move(item));
  Save(items);
  NotifyChanged();
  return new_id;
}

void SidebarItemStore::RemoveItem(const std::string& id) {
  RefreshCacheIfNeeded();
  auto items = cached_;
  const size_t before = items.size();
  std::erase_if(items, [&id](const SidebarItem& i) { return i.id == id; });
  if (items.size() == before) {
    return;
  }
  Save(items);
  NotifyChanged();
}

void SidebarItemStore::RemoveItemsForSpace(const std::string& space_id) {
  if (space_id.empty()) {
    return;  // Never mass-delete global items.
  }
  RefreshCacheIfNeeded();
  auto items = cached_;
  const size_t before = items.size();
  std::erase_if(items, [&space_id](const SidebarItem& i) {
    return i.space_id == space_id;
  });
  if (items.size() == before) {
    return;
  }
  Save(items);
  NotifyChanged();
}

void SidebarItemStore::MoveItemToSpace(
    const std::string& id,
    const std::string& destination_space_id) {
  RefreshCacheIfNeeded();
  auto items = cached_;
  for (auto& item : items) {
    if (item.id == id) {
      if (item.space_id == destination_space_id) {
        return;
      }
      item.space_id = destination_space_id;
      item.order = NextOrderFor(items, destination_space_id, item.type);
      break;
    }
  }
  Save(items);
  NotifyChanged();
}

void SidebarItemStore::SetItemType(const std::string& id,
                                    SidebarItemType type) {
  RefreshCacheIfNeeded();
  auto items = cached_;
  for (auto& item : items) {
    if (item.id == id) {
      if (item.type == type) {
        return;
      }
      item.type = type;
      item.order = NextOrderFor(items, item.space_id, type);
      break;
    }
  }
  Save(items);
  NotifyChanged();
}

void SidebarItemStore::SetItemTitle(const std::string& id,
                                    const std::string& title) {
  RefreshCacheIfNeeded();
  auto items = cached_;
  for (auto& item : items) {
    if (item.id == id) {
      item.title = title;
      break;
    }
  }
  Save(items);
  NotifyChanged();
}

void SidebarItemStore::ReorderItem(const std::string& id, int new_order) {
  RefreshCacheIfNeeded();
  auto items = cached_;
  for (auto& item : items) {
    if (item.id == id) {
      item.order = new_order;
      break;
    }
  }
  Save(items);
  NotifyChanged();
}

void SidebarItemStore::ReorderItems(
    const std::vector<std::string>& ordered_ids) {
  RefreshCacheIfNeeded();
  auto items = cached_;

  bool changed = false;
  for (size_t i = 0; i < ordered_ids.size(); ++i) {
    for (auto& item : items) {
      if (item.id != ordered_ids[i]) {
        continue;
      }
      if (item.order != static_cast<int>(i)) {
        item.order = static_cast<int>(i);
        changed = true;
      }
      break;
    }
  }

  if (!changed) {
    return;
  }
  Save(items);
  NotifyChanged();
}

void SidebarItemStore::ReplaceItemsOfType(SidebarItemType type,
                                          const std::vector<SidebarItem>& items,
                                          bool notify) {
  RefreshCacheIfNeeded();

  std::vector<SidebarItem> next;
  for (const auto& item : cached_) {
    if (item.type != type) {
      next.push_back(item);
    }
  }

  const base::Time now = base::Time::Now();
  int order = 0;
  for (SidebarItem item : items) {
    item.type = type;
    if (item.id.empty()) {
      item.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
    }
    if (item.created_at.is_null()) {
      item.created_at = now;
    }
    if (item.last_accessed_at.is_null()) {
      item.last_accessed_at = now;
    }
    item.order = order++;
    next.push_back(std::move(item));
  }

  Save(next);
  if (notify) {
    NotifyChanged();
  }
}

void SidebarItemStore::TouchItem(const std::string& id) {
  RefreshCacheIfNeeded();
  auto items = cached_;
  for (auto& item : items) {
    if (item.id == id) {
      item.last_accessed_at = base::Time::Now();
      break;
    }
  }
  Save(items);
  // Deliberately no notification: a visit doesn't change sidebar structure.
}

int SidebarItemStore::SweepExpiredTodayItems(
    base::TimeDelta max_age,
    const std::set<std::pair<std::string, std::string>>& protected_tabs) {
  if (!max_age.is_positive()) {
    return 0;
  }
  RefreshCacheIfNeeded();
  auto items = cached_;
  const base::Time cutoff = base::Time::Now() - max_age;
  const size_t before = items.size();
  std::erase_if(items, [&cutoff, &protected_tabs](const SidebarItem& i) {
    if (i.type != SidebarItemType::kToday) {
      return false;
    }
    if (protected_tabs.contains({i.space_id, i.url})) {
      return false;
    }
    return i.last_accessed_at < cutoff;
  });
  const int removed = static_cast<int>(before - items.size());
  if (removed > 0) {
    Save(items);
    NotifyChanged();
  }
  return removed;
}

void SidebarItemStore::Save(const std::vector<SidebarItem>& items) {
  if (pref_available_) {
    base::ListValue list;
    for (const auto& item : items) {
      list.Append(item.ToDict());
    }
    writing_ = true;
    pref_service_->SetList(kItemsPref, std::move(list));
    writing_ = false;
  }
  cached_ = items;
  cache_dirty_ = false;
}

}  // namespace avora
