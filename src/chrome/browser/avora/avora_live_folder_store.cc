// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_live_folder_store.h"

#include <algorithm>
#include <map>
#include <utility>

#include "base/functional/bind.h"
#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace avora {

namespace {

constexpr char kDefaultPullRequestsName[] = "Pull Requests";

std::vector<LiveFolder> FoldersFromList(const base::ListValue& list) {
  std::vector<LiveFolder> folders;
  for (const base::Value& value : list) {
    if (value.is_dict()) {
      folders.push_back(LiveFolder::FromDict(value.GetDict()));
    }
  }
  std::stable_sort(folders.begin(), folders.end(),
                   [](const LiveFolder& a, const LiveFolder& b) {
                     return a.order < b.order;
                   });
  return folders;
}

base::ListValue FoldersToList(const std::vector<LiveFolder>& folders) {
  base::ListValue list;
  for (const LiveFolder& folder : folders) {
    list.Append(folder.ToDict());
  }
  return list;
}

}  // namespace

LiveFolderStore::LiveFolderStore(PrefService* pref_service)
    : pref_service_(pref_service) {
  if (!pref_service_) {
    return;
  }

  pref_available_ = pref_service_->FindPreference(kLiveFoldersPref) != nullptr;

  space_manager_ = std::make_unique<SpaceManager>(pref_service_);
  space_manager_->AddObserver(this);

  if (pref_available_) {
    pref_registrar_.Init(pref_service_);
    pref_registrar_.Add(
        kLiveFoldersPref,
        base::BindRepeating(&LiveFolderStore::OnLiveFoldersPrefChanged,
                            base::Unretained(this)));
  }
}

LiveFolderStore::~LiveFolderStore() {
  if (space_manager_) {
    space_manager_->RemoveObserver(this);
  }
}

// static
void LiveFolderStore::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kLiveFoldersPref);
  registry->RegisterDictionaryPref(kDismissedFoldersPref);
}

// ── Space scoping ───────────────────────────────────────────────────────────

std::string LiveFolderStore::active_space_id() const {
  if (space_manager_) {
    if (const Space* active = space_manager_->GetActiveSpace()) {
      return active->id;
    }
  }
  return std::string();
}

std::vector<LiveFolder> LiveFolderStore::FoldersForSpace(
    const std::string& space_id) const {
  if (!pref_available_ || space_id.empty()) {
    return {};
  }
  const base::DictValue& by_space = pref_service_->GetDict(kLiveFoldersPref);
  const base::ListValue* list = by_space.FindList(space_id);
  if (!list) {
    return {};
  }
  return FoldersFromList(*list);
}

void LiveFolderStore::SaveFolders(const std::string& space_id,
                                  const std::vector<LiveFolder>& folders) {
  if (!pref_available_ || space_id.empty()) {
    return;
  }
  writing_ = true;
  ScopedDictPrefUpdate update(pref_service_, kLiveFoldersPref);
  update->Set(space_id, FoldersToList(folders));
  writing_ = false;
  NotifyChanged();
}

void LiveFolderStore::UpdateFolder(
    const std::string& folder_id,
    base::FunctionRef<void(LiveFolder&)> mutation) {
  const std::string space_id = active_space_id();
  std::vector<LiveFolder> folders = FoldersForSpace(space_id);
  auto it = std::find_if(folders.begin(), folders.end(),
                         [&folder_id](const LiveFolder& folder) {
                           return folder.id == folder_id;
                         });
  if (it == folders.end()) {
    return;
  }
  mutation(*it);
  SaveFolders(space_id, folders);
}

// ── Queries ─────────────────────────────────────────────────────────────────

std::vector<LiveFolder> LiveFolderStore::GetFolders() const {
  return FoldersForSpace(active_space_id());
}

std::vector<LiveFolder> LiveFolderStore::GetFoldersForSpace(
    const std::string& space_id) const {
  return FoldersForSpace(space_id);
}

std::optional<LiveFolder> LiveFolderStore::GetFolderById(
    const std::string& folder_id) const {
  for (LiveFolder& folder : FoldersForSpace(active_space_id())) {
    if (folder.id == folder_id) {
      return std::move(folder);
    }
  }
  return std::nullopt;
}

bool LiveFolderStore::HasFolder(const std::string& folder_id) const {
  return GetFolderById(folder_id).has_value();
}

// ── Mutations ───────────────────────────────────────────────────────────────

bool LiveFolderStore::WasFolderDismissed(const std::string& folder_id) const {
  const std::string space_id = active_space_id();
  if (!pref_service_ || space_id.empty() ||
      !pref_service_->FindPreference(kDismissedFoldersPref)) {
    return false;
  }
  const base::DictValue& by_space =
      pref_service_->GetDict(kDismissedFoldersPref);
  const base::ListValue* dismissed = by_space.FindList(space_id);
  if (!dismissed) {
    return false;
  }
  return std::any_of(dismissed->begin(), dismissed->end(),
                     [&folder_id](const base::Value& value) {
                       return value.is_string() &&
                              value.GetString() == folder_id;
                     });
}

std::string LiveFolderStore::EnsureGitHubPullRequestsFolder() {
  const std::string space_id = active_space_id();
  if (space_id.empty()) {
    return std::string();
  }

  // An explicit request overrides a past dismissal; otherwise the folder could
  // never be brought back once removed.
  if (pref_service_ && pref_service_->FindPreference(kDismissedFoldersPref)) {
    ScopedDictPrefUpdate update(pref_service_, kDismissedFoldersPref);
    if (base::ListValue* dismissed = update->FindList(space_id)) {
      dismissed->EraseValue(base::Value(kGitHubPullRequestsFolderId));
    }
  }

  std::vector<LiveFolder> folders = FoldersForSpace(space_id);
  for (const LiveFolder& folder : folders) {
    if (folder.id == kGitHubPullRequestsFolderId) {
      return folder.id;
    }
  }

  LiveFolder folder;
  folder.id = kGitHubPullRequestsFolderId;
  folder.space_id = space_id;
  folder.name = kDefaultPullRequestsName;
  folder.provider = LiveFolderProviderId::kGitHub;
  folder.query = LiveFolderQuery::DefaultForGitHub();
  folder.expanded = true;
  folder.order = static_cast<int>(folders.size());

  folders.push_back(std::move(folder));
  SaveFolders(space_id, folders);
  return kGitHubPullRequestsFolderId;
}

void LiveFolderStore::RemoveFolder(const std::string& folder_id) {
  const std::string space_id = active_space_id();
  std::vector<LiveFolder> folders = FoldersForSpace(space_id);
  const size_t before = folders.size();
  std::erase_if(folders, [&folder_id](const LiveFolder& folder) {
    return folder.id == folder_id;
  });
  if (folders.size() == before) {
    return;
  }

  // Remember the removal so provider detection does not put the folder back.
  if (pref_service_ && pref_service_->FindPreference(kDismissedFoldersPref)) {
    ScopedDictPrefUpdate update(pref_service_, kDismissedFoldersPref);
    base::ListValue* dismissed = update->EnsureList(space_id);
    const bool already_listed =
        std::any_of(dismissed->begin(), dismissed->end(),
                    [&folder_id](const base::Value& value) {
                      return value.is_string() &&
                             value.GetString() == folder_id;
                    });
    if (!already_listed) {
      dismissed->Append(folder_id);
    }
  }

  SaveFolders(space_id, folders);
}

void LiveFolderStore::RemoveFoldersForSpace(const std::string& space_id) {
  if (!pref_available_ || space_id.empty()) {
    return;
  }
  writing_ = true;
  ScopedDictPrefUpdate update(pref_service_, kLiveFoldersPref);
  update->Remove(space_id);
  writing_ = false;
  NotifyChanged();
}

void LiveFolderStore::ReplaceItems(const std::string& folder_id,
                                   const std::vector<LiveFolderItem>& items) {
  UpdateFolder(folder_id, [&items](LiveFolder& folder) {
    // Remember which items the user had already seen before this sync, keyed
    // by the provider's stable id.  Matching on anything else (title, URL)
    // would re-announce a pull request whose title was edited.
    std::map<std::string, bool> seen_before;
    for (const LiveFolderItem& existing : folder.items) {
      seen_before[existing.external_id] = existing.seen;
    }

    std::vector<LiveFolderItem> next;
    next.reserve(items.size());
    for (LiveFolderItem item : items) {
      const auto it = seen_before.find(item.external_id);
      // Anything the previous sync didn't have is new, and stays unseen until
      // the user actually looks at the folder.
      item.seen = it != seen_before.end() && it->second;
      next.push_back(std::move(item));
    }

    folder.items = std::move(next);
    folder.last_synced_at = base::Time::Now();
  });
}

void LiveFolderStore::MarkSynced(const std::string& folder_id) {
  UpdateFolder(folder_id, [](LiveFolder& folder) {
    folder.last_synced_at = base::Time::Now();
  });
}

void LiveFolderStore::SetExpanded(const std::string& folder_id,
                                  bool expanded) {
  UpdateFolder(folder_id, [expanded](LiveFolder& folder) {
    folder.expanded = expanded;
    if (expanded) {
      // Expanding is the user looking at the rows, which is exactly what the
      // unseen badge was there to prompt.
      for (LiveFolderItem& item : folder.items) {
        item.seen = true;
      }
    }
  });
}

void LiveFolderStore::SetName(const std::string& folder_id,
                              const std::string& name) {
  if (name.empty()) {
    return;
  }
  UpdateFolder(folder_id,
               [&name](LiveFolder& folder) { folder.name = name; });
}

void LiveFolderStore::SetQuery(const std::string& folder_id,
                               const LiveFolderQuery& query) {
  UpdateFolder(folder_id, [&query](LiveFolder& folder) {
    folder.query = query;
    // The previous contents answered a different question, so drop them rather
    // than show stale rows until the next sync lands.
    folder.items.clear();
    folder.last_synced_at = base::Time();
  });
}

void LiveFolderStore::MarkAllItemsSeen(const std::string& folder_id) {
  UpdateFolder(folder_id, [](LiveFolder& folder) {
    for (LiveFolderItem& item : folder.items) {
      item.seen = true;
    }
  });
}

void LiveFolderStore::MarkItemSeen(const std::string& folder_id,
                                   const std::string& external_id) {
  UpdateFolder(folder_id, [&external_id](LiveFolder& folder) {
    for (LiveFolderItem& item : folder.items) {
      if (item.external_id == external_id) {
        item.seen = true;
        return;
      }
    }
  });
}

// ── Observers ───────────────────────────────────────────────────────────────

void LiveFolderStore::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void LiveFolderStore::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void LiveFolderStore::OnActiveSpaceChanged(const std::string& space_id) {
  // A different Space is showing, so the visible folder set changed wholesale.
  NotifyChanged();
}

void LiveFolderStore::OnLiveFoldersPrefChanged() {
  if (writing_) {
    return;
  }
  NotifyChanged();
}

void LiveFolderStore::NotifyChanged() {
  for (Observer& observer : observers_) {
    observer.OnLiveFoldersChanged();
  }
}

}  // namespace avora
