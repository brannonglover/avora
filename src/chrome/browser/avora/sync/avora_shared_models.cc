// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_shared_models.h"

#include <algorithm>
#include <map>
#include <set>

#include "base/strings/string_util.h"
#include "chrome/browser/avora/avora_pinned_folders.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "url/gurl.h"

namespace avora::sync {

namespace {

constexpr char kSpacesContainer[] = "spaces";

const char* KindFor(SidebarItemType type) {
  return type == SidebarItemType::kFavorite ? kKindFavorite : kKindPinned;
}

std::optional<SidebarItemType> TypeForKind(std::string_view kind) {
  if (kind == kKindFavorite) {
    return SidebarItemType::kFavorite;
  }
  if (kind == kKindPinned) {
    return SidebarItemType::kPinned;
  }
  return std::nullopt;
}

bool IsShareableUrl(const std::string& url) {
  const GURL gurl(url);
  return gurl.is_valid() && gurl.SchemeIsHTTPOrHTTPS();
}

std::string StringColumn(const SharedRow& row, std::string_view column) {
  const std::string* value = row.columns.FindString(column);
  return value ? *value : std::string();
}

}  // namespace

SharedModels::SharedModels(PrefService* prefs, base::RepeatingClosure on_changed)
    : prefs_(prefs),
      on_changed_(std::move(on_changed)),
      space_manager_(prefs),
      item_store_(prefs) {
  space_observation_.Observe(&space_manager_);
  item_observation_.Observe(&item_store_);
}

SharedModels::~SharedModels() = default;

std::vector<LocalRow> SharedModels::ProjectSpaces() const {
  std::vector<LocalRow> rows;
  for (const Space& space : space_manager_.GetSpaces()) {
    LocalRow row;
    row.local_id = space.id;
    row.columns.Set("name", space.name);
    row.columns.Set("icon", space.icon);
    row.columns.Set("color", space.accent_color);
    row.container = kSpacesContainer;
    row.index = space.order;
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<LocalRow> SharedModels::ProjectSidebar() const {
  std::set<std::string> spaces;
  for (const Space& space : space_manager_.GetSpaces()) {
    spaces.insert(space.id);
  }
  std::vector<LocalRow> rows;
  for (const SidebarItem& item : item_store_.GetAllItems()) {
    if ((item.type != SidebarItemType::kFavorite &&
         item.type != SidebarItemType::kPinned) ||
        !spaces.count(item.space_id) || !IsShareableUrl(item.url)) {
      continue;
    }
    LocalRow row;
    row.local_id = item.id;
    row.columns.Set("space_id", item.space_id);
    row.columns.Set("kind", KindFor(item.type));
    row.columns.Set("url", item.url);
    row.columns.Set("title", item.title);
    row.container = item.space_id + "/" + KindFor(item.type);
    row.index = item.order;
    rows.push_back(std::move(row));
  }
  return rows;
}

void SharedModels::LinkMatching(SharedStore& store) const {
  std::set<std::string> taken;
  for (const Space& space : space_manager_.GetSpaces()) {
    for (const SharedRow* row : store.Rows(SharedTable::kSpaces)) {
      if (row->deleted || taken.count(row->id)) {
        continue;
      }
      if (base::EqualsCaseInsensitiveASCII(StringColumn(*row, "name"),
                                           space.name)) {
        store.Link(SharedTable::kSpaces, row->id, space.id);
        taken.insert(row->id);
        break;
      }
    }
  }

  taken.clear();
  for (const SidebarItem& item : item_store_.GetAllItems()) {
    if (item.type != SidebarItemType::kFavorite &&
        item.type != SidebarItemType::kPinned) {
      continue;
    }
    const std::string shared_space =
        store.ToShared(SharedTable::kSpaces, item.space_id);
    for (const SharedRow* row : store.ItemsIn(shared_space, KindFor(item.type))) {
      if (!taken.count(row->id) && StringColumn(*row, "url") == item.url) {
        store.Link(SharedTable::kItems, row->id, item.id);
        taken.insert(row->id);
        break;
      }
    }
  }
}

std::string SharedModels::LocalSpaceId(const SharedStore& store,
                                        const std::string& shared_space_id) const {
  const std::string local = store.ToLocal(SharedTable::kSpaces, shared_space_id);
  return space_manager_.GetSpaceById(local) ? local : std::string();
}

void SharedModels::Apply(SharedStore& store,
                         const std::vector<SharedRow>& spaces,
                         const std::vector<SharedRow>& items) {
  ApplySpaces(store, spaces);
  ApplyItems(store, items);
}

void SharedModels::ApplySpaces(SharedStore& store,
                               const std::vector<SharedRow>& rows) {
  if (rows.empty()) {
    return;
  }
  for (const SharedRow& row : rows) {
    const std::string local = store.ToLocal(SharedTable::kSpaces, row.id);
    const Space* existing = space_manager_.GetSpaceById(local);
    if (row.deleted) {
      if (existing) {
        RemoveSpace(local);
      }
      continue;
    }
    const std::string name = StringColumn(row, "name");
    const std::string icon = StringColumn(row, "icon");
    const std::string color = StringColumn(row, "color");
    if (existing) {
      if (existing->name != name || existing->icon != icon ||
          existing->accent_color != color) {
        space_manager_.UpdateSpace(local, name, icon, color);
      }
    } else {
      const std::string created = space_manager_.CreateSpace(
          name.empty() ? "Space" : name, icon, std::string(), color);
      store.Link(SharedTable::kSpaces, row.id, created);
    }
  }

  // Order: the shared Spaces in position order, then any this device has
  // that the account doesn't (yet).
  std::vector<const SharedRow*> ordered;
  for (const SharedRow* row : store.Rows(SharedTable::kSpaces)) {
    if (!row->deleted) {
      ordered.push_back(row);
    }
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const SharedRow* a, const SharedRow* b) {
              const std::string pa = StringColumn(*a, "position");
              const std::string pb = StringColumn(*b, "position");
              return pa != pb ? pa < pb : a->id < b->id;
            });
  std::vector<std::string> wanted;
  for (const SharedRow* row : ordered) {
    const std::string local = store.ToLocal(SharedTable::kSpaces, row->id);
    if (space_manager_.GetSpaceById(local)) {
      wanted.push_back(local);
    }
  }
  std::vector<std::string> current;
  for (const Space& space : space_manager_.GetSpaces()) {
    current.push_back(space.id);
  }
  std::vector<std::string> current_shared;
  for (const std::string& id : current) {
    if (std::ranges::contains(wanted, id)) {
      current_shared.push_back(id);
    }
  }
  if (current_shared != wanted) {
    for (size_t i = 0; i < wanted.size(); ++i) {
      space_manager_.ReorderSpace(wanted[i], static_cast<int>(i));
    }
  }
}

void SharedModels::ApplyItems(SharedStore& store,
                              const std::vector<SharedRow>& rows) {
  std::set<std::pair<std::string, std::string>> touched;  // (shared space, kind)
  for (const SharedRow& row : rows) {
    const std::optional<SidebarItemType> type =
        TypeForKind(StringColumn(row, "kind"));
    const std::string local = store.ToLocal(SharedTable::kItems, row.id);
    // A copy: the store's pointers don't survive its next write.
    std::optional<SidebarItem> existing;
    if (const SidebarItem* item = item_store_.GetItemById(local);
        item && (item->type == SidebarItemType::kFavorite ||
                 item->type == SidebarItemType::kPinned)) {
      existing = *item;
    }

    // Deleted, or no longer a Favorite or pinned item (it became a today
    // tab, which TabSyncService opens).
    if (row.deleted || !type) {
      if (existing) {
        RemoveItem(*existing);
      }
      continue;
    }

    const std::string space_id =
        LocalSpaceId(store, StringColumn(row, "space_id"));
    const std::string url = StringColumn(row, "url");
    if (space_id.empty() || !IsShareableUrl(url)) {
      continue;
    }
    touched.insert({StringColumn(row, "space_id"), StringColumn(row, "kind")});
    const std::string title = StringColumn(row, "title");

    if (existing && existing->url != url) {
      // The store has no URL setter: replace the item and relink.
      RemoveItem(*existing);
      existing.reset();
    }
    if (!existing) {
      const std::string created = item_store_.AddItem(space_id, *type, url, title);
      store.Link(SharedTable::kItems, row.id, created);
      continue;
    }
    if (existing->space_id != space_id) {
      item_store_.MoveItemToSpace(local, space_id);
    }
    if (existing->type != *type) {
      item_store_.SetItemType(local, *type);
    }
    if (existing->title != title) {
      item_store_.SetItemTitle(local, title);
    }
  }

  // Order within each touched (Space, kind): shared rows by position, then
  // local items the account doesn't have yet.
  for (const auto& [shared_space, kind] : touched) {
    const std::string space_id = LocalSpaceId(store, shared_space);
    const std::optional<SidebarItemType> type = TypeForKind(kind);
    if (space_id.empty() || !type) {
      continue;
    }
    std::vector<std::string> wanted;
    for (const SharedRow* row : store.ItemsIn(shared_space, kind)) {
      const std::string local = store.ToLocal(SharedTable::kItems, row->id);
      const SidebarItem* item = item_store_.GetItemById(local);
      if (item && item->space_id == space_id && item->type == *type) {
        wanted.push_back(local);
      }
    }
    std::vector<SidebarItem> current = item_store_.GetItems(space_id, *type);
    std::sort(current.begin(), current.end(),
              [](const SidebarItem& a, const SidebarItem& b) {
                return a.order < b.order;
              });
    std::vector<std::string> ordered = wanted;
    std::vector<std::string> current_ids;
    for (const SidebarItem& item : current) {
      current_ids.push_back(item.id);
      if (!std::ranges::contains(wanted, item.id)) {
        ordered.push_back(item.id);
      }
    }
    if (ordered != current_ids) {
      item_store_.ReorderItems(ordered);
    }
  }
}

void SharedModels::RemoveItem(const SidebarItem& item) {
  if (item.type == SidebarItemType::kPinned && pinned_item_remover_) {
    pinned_item_remover_.Run(item.id);
  }
  if (item_store_.GetItemById(item.id)) {
    item_store_.RemoveItem(item.id);
  }
}

void SharedModels::RemoveSpace(const std::string& local_id) {
  // As deleting a Space from the Spaces bar does: its sidebar items and
  // pinned-folder data go with it. SpaceManager refuses to remove the last
  // Space, which then simply stays.
  if (space_manager_.GetSpaces().size() <= 1) {
    return;
  }
  item_store_.RemoveItemsForSpace(local_id);
  {
    ScopedDictPrefUpdate folders(
        prefs_, PinnedFoldersManager::kPinnedFoldersBySpacePref);
    folders->Remove(local_id);
  }
  {
    ScopedDictPrefUpdate titles(
        prefs_, PinnedFoldersManager::kPinnedTabTitlesBySpacePref);
    titles->Remove(local_id);
  }
  space_manager_.RemoveSpace(local_id);
}

void SharedModels::OnSpacesChanged() {
  on_changed_.Run();
}

void SharedModels::OnSidebarItemsChanged() {
  on_changed_.Run();
}

}  // namespace avora::sync
