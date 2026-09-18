// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_pinned_folders.h"

#include <algorithm>
#include <set>

#include "base/functional/bind.h"
#include "base/uuid.h"
#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace avora {

namespace {

std::vector<PinnedFolder> FoldersFromList(const base::ListValue& list) {
  std::vector<PinnedFolder> result;
  for (const auto& val : list) {
    if (!val.is_dict()) {
      continue;
    }
    const auto& dict = val.GetDict();
    PinnedFolder folder;
    if (const std::string* id = dict.FindString("id")) {
      folder.id = *id;
    }
    const std::string* name = dict.FindString("name");
    folder.name = name ? *name : "Untitled";
    folder.expanded = dict.FindBool("expanded").value_or(true);
    if (const base::ListValue* ids = dict.FindList("ordered_item_ids")) {
      for (const auto& id_val : *ids) {
        if (id_val.is_string() && !id_val.GetString().empty()) {
          folder.ordered_item_ids.push_back(id_val.GetString());
        }
      }
    }
    result.push_back(std::move(folder));
  }
  return result;
}

base::ListValue FoldersToList(const std::vector<PinnedFolder>& folders) {
  base::ListValue list;
  for (const auto& folder : folders) {
    base::ListValue ids;
    for (const auto& id : folder.ordered_item_ids) {
      ids.Append(id);
    }
    base::DictValue entry;
    entry.Set("id", folder.id);
    entry.Set("name", folder.name);
    entry.Set("expanded", folder.expanded);
    entry.Set("ordered_item_ids", std::move(ids));
    list.Append(std::move(entry));
  }
  return list;
}

}  // namespace

PinnedFoldersManager::PinnedFoldersManager(PrefService* prefs) : prefs_(prefs) {
  if (!prefs_) {
    return;
  }

  pref_available_ =
      prefs_->FindPreference(kPinnedFoldersBySpacePref) != nullptr;

  space_manager_ = std::make_unique<SpaceManager>(prefs_);
  pinned_items_manager_ = std::make_unique<PinnedItemsManager>(prefs_);

  MigrateLegacyPinnedIfNeeded();
  MigrateFolderSchemaIfNeeded();

  space_manager_->AddObserver(this);

  if (pref_available_) {
    pref_registrar_.Init(prefs_);
    pref_registrar_.Add(
        kPinnedFoldersBySpacePref,
        base::BindRepeating(&PinnedFoldersManager::OnPinnedPrefsChanged,
                            base::Unretained(this)));
    pref_registrar_.Add(
        kPinnedTabTitlesBySpacePref,
        base::BindRepeating(&PinnedFoldersManager::OnPinnedPrefsChanged,
                            base::Unretained(this)));
  }
}

PinnedFoldersManager::~PinnedFoldersManager() {
  if (space_manager_) {
    space_manager_->RemoveObserver(this);
  }
}

// static
void PinnedFoldersManager::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kPinnedFoldersBySpacePref);
  registry->RegisterDictionaryPref(kPinnedTabTitlesBySpacePref);
  registry->RegisterListPref(kPinnedFoldersPref);
  registry->RegisterDictionaryPref(kPinnedTabTitlesPref);
  registry->RegisterBooleanPref(kPinnedFoldersMigratedPref, false);
  registry->RegisterBooleanPref(kPinnedFolderSchemaMigratedPref, false);
}

// ── Space scoping ───────────────────────────────────────────────────────────

std::string PinnedFoldersManager::GetActiveSpaceId() const {
  return ActiveSpaceId();
}

std::string PinnedFoldersManager::ActiveSpaceId() const {
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

void PinnedFoldersManager::SetWindowActiveSpaceId(const std::string& id) {
  if (id == window_active_space_id_) {
    return;
  }
  window_active_space_id_ = id;
  if (pinned_items_manager_) {
    pinned_items_manager_->SetWindowActiveSpaceId(id);
  }
  NotifyChanged();
}

std::vector<PinnedFolder> PinnedFoldersManager::ActiveFolders() const {
  if (!prefs_ || !pref_available_) {
    return {};
  }
  const std::string space_id = ActiveSpaceId();
  if (space_id.empty()) {
    return {};
  }
  const base::DictValue& by_space = prefs_->GetDict(kPinnedFoldersBySpacePref);
  const base::ListValue* list = by_space.FindList(space_id);
  if (!list) {
    return {};
  }
  return FoldersFromList(*list);
}

// ── Migration ───────────────────────────────────────────────────────────────

void PinnedFoldersManager::MigrateLegacyPinnedIfNeeded() {
  if (!prefs_ || !pref_available_) {
    return;
  }
  // Without the flag pref there is no safe way to know whether we already ran,
  // so do nothing rather than risk importing twice.
  if (!prefs_->FindPreference(kPinnedFoldersMigratedPref)) {
    return;
  }
  if (prefs_->GetBoolean(kPinnedFoldersMigratedPref)) {
    return;
  }

  const std::string space_id = ActiveSpaceId();
  if (space_id.empty()) {
    return;
  }

  writing_ = true;

  if (prefs_->FindPreference(kPinnedFoldersPref)) {
    const base::ListValue& legacy = prefs_->GetList(kPinnedFoldersPref);
    if (!legacy.empty()) {
      const base::DictValue& existing_by_space =
          prefs_->GetDict(kPinnedFoldersBySpacePref);
      const base::ListValue* existing =
          existing_by_space.FindList(space_id);
      if (!existing || existing->empty()) {
        base::ListValue copy;
        for (const auto& val : legacy) {
          copy.Append(val.Clone());
        }
        ScopedDictPrefUpdate folders_update(prefs_, kPinnedFoldersBySpacePref);
        folders_update->Set(space_id, std::move(copy));
      }
    }
  }

  if (prefs_->FindPreference(kPinnedTabTitlesPref)) {
    const base::DictValue& legacy_titles =
        prefs_->GetDict(kPinnedTabTitlesPref);
    if (!legacy_titles.empty()) {
      const base::DictValue& existing_by_space =
          prefs_->GetDict(kPinnedTabTitlesBySpacePref);
      const base::DictValue* existing = existing_by_space.FindDict(space_id);
      if (!existing || existing->empty()) {
        base::DictValue copy;
        for (const auto [key, value] : legacy_titles) {
          copy.Set(key, value.Clone());
        }
        ScopedDictPrefUpdate titles_update(prefs_,
                                           kPinnedTabTitlesBySpacePref);
        titles_update->Set(space_id, std::move(copy));
      }
    }
  }

  prefs_->SetBoolean(kPinnedFoldersMigratedPref, true);
  writing_ = false;
}

void PinnedFoldersManager::MigrateFolderSchemaIfNeeded() {
  if (!prefs_ || !pref_available_ || !pinned_items_manager_) {
    return;
  }
  if (!prefs_->FindPreference(kPinnedFolderSchemaMigratedPref)) {
    return;
  }
  if (prefs_->GetBoolean(kPinnedFolderSchemaMigratedPref)) {
    return;
  }

  // Read-copy-write-once, like every other store in this codebase, rather
  // than mutating the live pref in place -- simpler to reason about and
  // matches ActiveFolders()/SaveFolders()'s own pattern.
  const base::DictValue original =
      prefs_->GetDict(kPinnedFoldersBySpacePref).Clone();
  base::DictValue migrated;

  for (const auto [space_id, value] : original) {
    if (!value.is_list()) {
      // Malformed entry -- carry it forward unchanged rather than dropping
      // it; migration must be non-destructive even for data it doesn't
      // understand.
      migrated.Set(space_id, value.Clone());
      continue;
    }

    // Legacy tab URLs are resolved (or created) as kPinned items scoped to
    // this specific Space, mirroring AvoraSpaceTabFilter::
    // BackfillNativePinnedTabs()'s resolve-or-create pattern.
    pinned_items_manager_->SetWindowActiveSpaceId(space_id);

    // The same URL was never preventable from appearing in two folders under
    // the old per-folder-only dedup, so first-folder-wins here to preserve
    // the "an item belongs to at most one folder" invariant this Phase
    // establishes -- a legacy state that could not previously arise through
    // normal use.
    std::set<std::string> claimed_urls;
    base::ListValue migrated_folders;

    for (const auto& folder_value : value.GetList()) {
      if (!folder_value.is_dict()) {
        continue;
      }
      const base::DictValue& folder_dict = folder_value.GetDict();
      base::DictValue new_folder;

      const std::string* existing_id = folder_dict.FindString("id");
      new_folder.Set("id", existing_id
                               ? *existing_id
                               : base::Uuid::GenerateRandomV4().AsLowercaseString());
      const std::string* name = folder_dict.FindString("name");
      new_folder.Set("name", name ? *name : "Untitled");
      new_folder.Set("expanded", folder_dict.FindBool("expanded").value_or(true));

      base::ListValue ordered_item_ids;
      if (const base::ListValue* existing_ids =
              folder_dict.FindList("ordered_item_ids")) {
        // Already-migrated data (idempotency: a second run sees this and
        // carries it forward rather than re-deriving it from "tabs", which
        // is no longer present anyway).
        for (const auto& id_value : *existing_ids) {
          ordered_item_ids.Append(id_value.Clone());
        }
      } else if (const base::ListValue* legacy_tabs =
                     folder_dict.FindList("tabs")) {
        for (const auto& tab_value : *legacy_tabs) {
          if (!tab_value.is_dict()) {
            continue;
          }
          const base::DictValue& tab_dict = tab_value.GetDict();
          const std::string* url = tab_dict.FindString("url");
          if (!url || url->empty() || claimed_urls.contains(*url)) {
            continue;
          }
          const std::string* title = tab_dict.FindString("title");
          std::string item_id =
              pinned_items_manager_->GetPinnedItemIdForUrl(*url);
          if (item_id.empty()) {
            item_id = pinned_items_manager_->AddPinnedItem(
                *url, title ? *title : std::string());
          }
          if (!item_id.empty()) {
            ordered_item_ids.Append(item_id);
            claimed_urls.insert(*url);
          }
        }
      }
      new_folder.Set("ordered_item_ids", std::move(ordered_item_ids));
      migrated_folders.Append(std::move(new_folder));
    }
    migrated.Set(space_id, std::move(migrated_folders));
  }

  writing_ = true;
  prefs_->SetDict(kPinnedFoldersBySpacePref, std::move(migrated));
  writing_ = false;

  // Restore normal (caller-driven) active-Space tracking now that the
  // per-Space migration loop above is done overriding it.
  pinned_items_manager_->SetWindowActiveSpaceId(window_active_space_id_);

  prefs_->SetBoolean(kPinnedFolderSchemaMigratedPref, true);
}

void PinnedFoldersManager::SaveFolders(
    const std::vector<PinnedFolder>& folders) {
  const std::string space_id = ActiveSpaceId();
  if (space_id.empty() || !prefs_ || !pref_available_) {
    return;
  }

  writing_ = true;
  ScopedDictPrefUpdate update(prefs_, kPinnedFoldersBySpacePref);
  update->Set(space_id, FoldersToList(folders));
  writing_ = false;
  NotifyChanged();
}

// ── Queries ─────────────────────────────────────────────────────────────────

std::vector<PinnedFolder> PinnedFoldersManager::GetFolders() const {
  return ActiveFolders();
}

std::string PinnedFoldersManager::GetFolderIdForItem(
    const std::string& pinned_item_id) const {
  if (pinned_item_id.empty()) {
    return std::string();
  }
  for (const auto& folder : ActiveFolders()) {
    if (std::find(folder.ordered_item_ids.begin(),
                 folder.ordered_item_ids.end(),
                 pinned_item_id) != folder.ordered_item_ids.end()) {
      return folder.id;
    }
  }
  return std::string();
}

std::string PinnedFoldersManager::GetCustomTabTitle(
    const std::string& url) const {
  if (!prefs_ || !pref_available_) {
    return std::string();
  }
  const std::string space_id = ActiveSpaceId();
  if (space_id.empty()) {
    return std::string();
  }
  const base::DictValue& by_space =
      prefs_->GetDict(kPinnedTabTitlesBySpacePref);
  const base::DictValue* titles = by_space.FindDict(space_id);
  if (!titles) {
    return std::string();
  }
  const std::string* title = titles->FindString(url);
  return title ? *title : std::string();
}

// ── Mutations ───────────────────────────────────────────────────────────────

std::string PinnedFoldersManager::AddFolder(const std::string& name) {
  std::vector<PinnedFolder> folders = ActiveFolders();
  PinnedFolder folder;
  folder.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  folder.name = name;
  folder.expanded = true;
  const std::string new_id = folder.id;
  folders.push_back(std::move(folder));
  SaveFolders(folders);
  return new_id;
}

void PinnedFoldersManager::RemoveFolder(const std::string& folder_id) {
  std::vector<PinnedFolder> folders = ActiveFolders();
  auto it = std::find_if(
      folders.begin(), folders.end(),
      [&folder_id](const PinnedFolder& f) { return f.id == folder_id; });
  if (it == folders.end()) {
    return;
  }

  // Folder deletion is organizational, never destructive: members are
  // promoted to top-level rather than removed, and neither their kPinned
  // records nor any live tab materialized from them is touched.
  const std::vector<std::string> promoted_ids = it->ordered_item_ids;
  folders.erase(it);
  SaveFolders(folders);

  if (promoted_ids.empty() || !pinned_items_manager_) {
    return;
  }

  // Preserve relative order: existing top-level items keep their current
  // relative order, and the promoted items are appended after them in
  // their prior in-folder order.
  std::set<std::string> claimed;
  for (const auto& folder : folders) {
    for (const auto& id : folder.ordered_item_ids) {
      claimed.insert(id);
    }
  }
  std::vector<std::string> full_order;
  for (const auto& item : pinned_items_manager_->GetPinnedItems()) {
    if (!claimed.contains(item.id) &&
        std::find(promoted_ids.begin(), promoted_ids.end(), item.id) ==
            promoted_ids.end()) {
      full_order.push_back(item.id);
    }
  }
  full_order.insert(full_order.end(), promoted_ids.begin(), promoted_ids.end());
  pinned_items_manager_->ReorderAllItems(full_order);
}

void PinnedFoldersManager::RenameFolder(const std::string& folder_id,
                                        const std::string& name) {
  std::vector<PinnedFolder> folders = ActiveFolders();
  for (auto& folder : folders) {
    if (folder.id == folder_id) {
      folder.name = name;
      SaveFolders(folders);
      return;
    }
  }
}

void PinnedFoldersManager::SetFolderExpanded(const std::string& folder_id,
                                             bool expanded) {
  std::vector<PinnedFolder> folders = ActiveFolders();
  for (auto& folder : folders) {
    if (folder.id == folder_id) {
      folder.expanded = expanded;
      SaveFolders(folders);
      return;
    }
  }
}

void PinnedFoldersManager::ReorderFolder(const std::string& folder_id,
                                         int new_index) {
  std::vector<PinnedFolder> folders = ActiveFolders();
  auto it = std::find_if(
      folders.begin(), folders.end(),
      [&folder_id](const PinnedFolder& f) { return f.id == folder_id; });
  if (it == folders.end()) {
    return;
  }
  const int count = static_cast<int>(folders.size());
  const int clamped = std::clamp(new_index, 0, count - 1);
  PinnedFolder moved = std::move(*it);
  folders.erase(it);
  folders.insert(folders.begin() + clamped, std::move(moved));
  SaveFolders(folders);
}

void PinnedFoldersManager::MoveItemToFolder(const std::string& pinned_item_id,
                                            const std::string& folder_id) {
  if (pinned_item_id.empty()) {
    return;
  }
  if (GetFolderIdForItem(pinned_item_id) == folder_id) {
    return;  // Already exactly there (including "already top-level").
  }

  std::vector<PinnedFolder> folders = ActiveFolders();
  for (auto& folder : folders) {
    std::erase(folder.ordered_item_ids, pinned_item_id);
  }
  if (!folder_id.empty()) {
    for (auto& folder : folders) {
      if (folder.id == folder_id) {
        folder.ordered_item_ids.push_back(pinned_item_id);
        break;
      }
    }
  }
  SaveFolders(folders);
}

void PinnedFoldersManager::ReorderItemInFolder(
    const std::string& folder_id,
    const std::string& pinned_item_id,
    int new_index) {
  std::vector<PinnedFolder> folders = ActiveFolders();
  for (auto& folder : folders) {
    if (folder.id != folder_id) {
      continue;
    }
    auto& ids = folder.ordered_item_ids;
    auto it = std::find(ids.begin(), ids.end(), pinned_item_id);
    if (it == ids.end()) {
      return;  // Not a member of this folder -- no-op.
    }
    const int count = static_cast<int>(ids.size());
    const int clamped = std::clamp(new_index, 0, count - 1);
    const std::string moved = *it;
    ids.erase(it);
    ids.insert(ids.begin() + clamped, moved);
    SaveFolders(folders);
    return;
  }
}

void PinnedFoldersManager::RemoveItemFromAllFolders(
    const std::string& pinned_item_id) {
  if (pinned_item_id.empty()) {
    return;
  }
  std::vector<PinnedFolder> folders = ActiveFolders();
  bool changed = false;
  for (auto& folder : folders) {
    const size_t before = folder.ordered_item_ids.size();
    std::erase(folder.ordered_item_ids, pinned_item_id);
    if (folder.ordered_item_ids.size() != before) {
      changed = true;
    }
  }
  if (changed) {
    SaveFolders(folders);
  }
}

void PinnedFoldersManager::SetCustomTabTitle(const std::string& url,
                                             const std::string& title) {
  const std::string space_id = ActiveSpaceId();
  if (space_id.empty() || !prefs_ || !pref_available_) {
    return;
  }

  writing_ = true;
  ScopedDictPrefUpdate by_space(prefs_, kPinnedTabTitlesBySpacePref);
  base::DictValue* titles = by_space->FindDict(space_id);
  if (!titles) {
    by_space->Set(space_id, base::DictValue());
    titles = by_space->FindDict(space_id);
  }
  if (titles) {
    titles->Set(url, title);
  }
  writing_ = false;
  // Title changes are cosmetic — the calling view already updated its
  // label locally.  Skip NotifyChanged() so we don't trigger a heavy
  // Rebuild().  The pref is still written, so cross-instance sync via
  // OnPinnedPrefsChanged() still works.
}

void PinnedFoldersManager::ClearCustomTabTitle(const std::string& url) {
  const std::string space_id = ActiveSpaceId();
  if (space_id.empty() || !prefs_ || !pref_available_) {
    return;
  }

  writing_ = true;
  ScopedDictPrefUpdate by_space(prefs_, kPinnedTabTitlesBySpacePref);
  if (base::DictValue* titles = by_space->FindDict(space_id)) {
    titles->Remove(url);
  }
  writing_ = false;
  // See SetCustomTabTitle — no rebuild needed for title removal.
}

// ── Observers ───────────────────────────────────────────────────────────────

void PinnedFoldersManager::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void PinnedFoldersManager::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void PinnedFoldersManager::OnActiveSpaceChanged(const std::string& space_id) {
  // When window-local active Space is set, the global pref change is
  // irrelevant for this instance; the owning view drives changes via
  // SetWindowActiveSpaceId() instead.
  if (!window_active_space_id_.empty()) {
    return;
  }
  // A different Space is showing, so the pinned folder set has changed
  // wholesale.
  NotifyChanged();
}

void PinnedFoldersManager::OnPinnedPrefsChanged() {
  if (writing_) {
    return;
  }
  NotifyChanged();
}

void PinnedFoldersManager::NotifyChanged() {
  for (auto& observer : observers_) {
    observer.OnPinnedFoldersChanged();
  }
}

void MovePinnedItemToSpace(PrefService* prefs,
                           const std::string& item_id,
                           const std::string& source_space_id,
                           const std::string& destination_space_id) {
  if (!prefs || item_id.empty() || destination_space_id.empty() ||
      source_space_id == destination_space_id) {
    return;
  }

  // Scrub first: RemoveItemFromAllFolders() works on whichever Space the
  // manager considers active, so it has to run against the source Space
  // while it is still named explicitly here.  Both halves are idempotent, so
  // a caller that has already done one of them costs nothing.
  if (!source_space_id.empty()) {
    PinnedFoldersManager folders(prefs);
    folders.SetWindowActiveSpaceId(source_space_id);
    folders.RemoveItemFromAllFolders(item_id);
  }

  // Fresh manager instances are enough here, exactly as in
  // AvoraSpaceTabFilter::BackfillNativePinnedTabs(): both are cheap,
  // self-syncing facades over the same SidebarItemStore pref, not resources
  // that need to outlive this call.
  PinnedItemsManager items(prefs);
  items.MoveItemToSpace(item_id, destination_space_id);
}

}  // namespace avora
