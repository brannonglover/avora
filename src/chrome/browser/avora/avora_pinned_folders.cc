// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_pinned_folders.h"

#include <algorithm>

#include "base/functional/bind.h"
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
    const std::string* name = dict.FindString("name");
    folder.name = name ? *name : "Untitled";
    folder.expanded = dict.FindBool("expanded").value_or(true);
    const auto* tabs = dict.FindList("tabs");
    if (tabs) {
      for (const auto& tab_val : *tabs) {
        if (!tab_val.is_dict()) {
          continue;
        }
        const auto& tab_dict = tab_val.GetDict();
        const std::string* url = tab_dict.FindString("url");
        const std::string* title = tab_dict.FindString("title");
        if (url) {
          folder.tabs.push_back({*url, title ? *title : std::string()});
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
    base::ListValue tabs;
    for (const auto& tab : folder.tabs) {
      base::DictValue tab_entry;
      tab_entry.Set("url", tab.url);
      tab_entry.Set("title", tab.title);
      tabs.Append(std::move(tab_entry));
    }
    base::DictValue entry;
    entry.Set("name", folder.name);
    entry.Set("expanded", folder.expanded);
    entry.Set("tabs", std::move(tabs));
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

  MigrateLegacyPinnedIfNeeded();

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

int PinnedFoldersManager::GetFolderIndexForTab(const std::string& url) const {
  const std::vector<PinnedFolder> folders = ActiveFolders();
  for (int i = 0; i < static_cast<int>(folders.size()); ++i) {
    for (const auto& tab : folders[i].tabs) {
      if (tab.url == url) {
        return i;
      }
    }
  }
  return -1;
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

void PinnedFoldersManager::AddFolder(const std::string& name) {
  std::vector<PinnedFolder> folders = ActiveFolders();
  PinnedFolder folder;
  folder.name = name;
  folder.expanded = true;
  folders.push_back(std::move(folder));
  SaveFolders(folders);
}

void PinnedFoldersManager::RemoveFolder(int index) {
  std::vector<PinnedFolder> folders = ActiveFolders();
  if (index < 0 || index >= static_cast<int>(folders.size())) {
    return;
  }
  folders.erase(folders.begin() + index);
  SaveFolders(folders);
}

void PinnedFoldersManager::RenameFolder(int index, const std::string& name) {
  std::vector<PinnedFolder> folders = ActiveFolders();
  if (index < 0 || index >= static_cast<int>(folders.size())) {
    return;
  }
  folders[index].name = name;
  SaveFolders(folders);
}

void PinnedFoldersManager::AddTabToFolder(int folder_index,
                                          const std::string& url,
                                          const std::string& title) {
  std::vector<PinnedFolder> folders = ActiveFolders();
  if (folder_index < 0 ||
      folder_index >= static_cast<int>(folders.size())) {
    return;
  }
  for (const auto& tab : folders[folder_index].tabs) {
    if (tab.url == url) {
      return;
    }
  }
  folders[folder_index].tabs.push_back({url, title});
  SaveFolders(folders);
}

void PinnedFoldersManager::RemoveTabFromFolder(int folder_index,
                                               const std::string& url) {
  std::vector<PinnedFolder> folders = ActiveFolders();
  if (folder_index < 0 ||
      folder_index >= static_cast<int>(folders.size())) {
    return;
  }
  auto& tabs = folders[folder_index].tabs;
  std::erase_if(tabs, [&url](const FolderTab& tab) { return tab.url == url; });
  SaveFolders(folders);
}

void PinnedFoldersManager::RemoveTabFromAllFolders(const std::string& url) {
  std::vector<PinnedFolder> folders = ActiveFolders();
  for (auto& folder : folders) {
    std::erase_if(folder.tabs,
                  [&url](const FolderTab& tab) { return tab.url == url; });
  }
  SaveFolders(folders);
}

void PinnedFoldersManager::MoveTabToFolder(const std::string& url,
                                           const std::string& title,
                                           int folder_index) {
  std::vector<PinnedFolder> folders = ActiveFolders();
  for (auto& folder : folders) {
    std::erase_if(folder.tabs,
                  [&url](const FolderTab& tab) { return tab.url == url; });
  }
  if (folder_index >= 0 &&
      folder_index < static_cast<int>(folders.size())) {
    folders[folder_index].tabs.push_back({url, title});
  }
  SaveFolders(folders);
}

void PinnedFoldersManager::SetFolderExpanded(int index, bool expanded) {
  std::vector<PinnedFolder> folders = ActiveFolders();
  if (index < 0 || index >= static_cast<int>(folders.size())) {
    return;
  }
  folders[index].expanded = expanded;
  SaveFolders(folders);
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

}  // namespace avora
