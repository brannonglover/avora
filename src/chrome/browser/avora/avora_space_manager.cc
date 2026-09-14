// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_space_manager.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/avora/avora_profile.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace avora {

namespace {

bool IsPrefRegistered(PrefService* ps) {
  return ps && ps->FindPreference(SpaceManager::kSpacesPref);
}

void SortByOrder(std::vector<Space>& spaces) {
  std::stable_sort(spaces.begin(), spaces.end(),
                   [](const Space& a, const Space& b) {
                     return a.order < b.order;
                   });
}

}  // namespace

SpaceManager::SpaceManager(PrefService* pref_service)
    : pref_service_(pref_service),
      pref_available_(IsPrefRegistered(pref_service)) {
  if (!pref_available_) {
    // In-memory fallback so the UI still has something to show.
    Space space;
    space.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
    space.name = "Default Space";
    space.icon = kDefaultSpaceIcons[0];
    space.profile_id = BrowserProfileStore::kDefaultProfileId;
    space.order = 0;
    space.is_active = true;
    cached_spaces_.push_back(std::move(space));
    cache_dirty_ = false;
    return;
  }

  if (GetSpaces().empty()) {
    CreateSpace("Default Space", kDefaultSpaceIcons[0],
                BrowserProfileStore::kDefaultProfileId);
  }

  if (const Space* active = GetActiveSpace()) {
    last_known_active_id_ = active->id;
  }

  pref_registrar_.Init(pref_service);
  pref_registrar_.Add(
      kSpacesPref,
      base::BindRepeating(&SpaceManager::OnSpacesPrefChanged,
                          base::Unretained(this)));
}

void SpaceManager::OnSpacesPrefChanged() {
  if (writing_) {
    // Our own write; observers were already notified synchronously.
    return;
  }

  cache_dirty_ = true;

  const Space* active = GetActiveSpace();
  const std::string active_id = active ? active->id : std::string();
  const bool active_changed = active_id != last_known_active_id_;
  last_known_active_id_ = active_id;

  NotifySpacesChanged();
  if (active_changed) {
    NotifyActiveSpaceChanged(active_id);
  }
}

SpaceManager::~SpaceManager() = default;

// static
void SpaceManager::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kSpacesPref);
}

// ── Observer management ─────────────────────────────────────────────────────

void SpaceManager::AddObserver(SpaceManagerObserver* observer) {
  observers_.AddObserver(observer);
}

void SpaceManager::RemoveObserver(SpaceManagerObserver* observer) {
  observers_.RemoveObserver(observer);
}

void SpaceManager::NotifySpacesChanged() {
  for (auto& observer : observers_) {
    observer.OnSpacesChanged();
  }
}

void SpaceManager::NotifyActiveSpaceChanged(const std::string& id) {
  for (auto& observer : observers_) {
    observer.OnActiveSpaceChanged(id);
  }
}

// ── Cache ───────────────────────────────────────────────────────────────────

void SpaceManager::RefreshCacheIfNeeded() const {
  if (!cache_dirty_) {
    return;
  }
  if (!pref_available_) {
    cache_dirty_ = false;
    return;
  }
  const base::ListValue& list = pref_service_->GetList(kSpacesPref);
  cached_spaces_.clear();
  for (const auto& val : list) {
    if (val.is_dict()) {
      cached_spaces_.push_back(Space::FromDict(val.GetDict()));
    }
  }
  SortByOrder(cached_spaces_);
  cache_dirty_ = false;
}

// ── Queries ─────────────────────────────────────────────────────────────────

std::vector<Space> SpaceManager::GetSpaces() const {
  RefreshCacheIfNeeded();
  return cached_spaces_;
}

const Space* SpaceManager::GetActiveSpace() const {
  RefreshCacheIfNeeded();
  for (const auto& space : cached_spaces_) {
    if (space.is_active) {
      return &space;
    }
  }
  if (!cached_spaces_.empty()) {
    return &cached_spaces_.front();
  }
  return nullptr;
}

const Space* SpaceManager::GetSpaceById(const std::string& id) const {
  RefreshCacheIfNeeded();
  for (const auto& space : cached_spaces_) {
    if (space.id == id) {
      return &space;
    }
  }
  return nullptr;
}

std::vector<Space> SpaceManager::GetSpacesForProfile(
    const std::string& profile_id) const {
  RefreshCacheIfNeeded();
  std::vector<Space> result;
  for (const auto& space : cached_spaces_) {
    if (space.profile_id == profile_id) {
      result.push_back(space);
    }
  }
  return result;
}

// ── Mutations ───────────────────────────────────────────────────────────────

std::string SpaceManager::CreateSpace(const std::string& name,
                                      const std::string& icon,
                                      const std::string& profile_id) {
  RefreshCacheIfNeeded();
  auto spaces = cached_spaces_;

  Space space;
  space.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  space.name = name;
  space.icon = icon.empty()
      ? kDefaultSpaceIcons[spaces.size() % kDefaultSpaceIconCount]
      : icon;
  space.profile_id = profile_id.empty()
      ? std::string(BrowserProfileStore::kDefaultProfileId)
      : profile_id;
  space.order = static_cast<int>(spaces.size());
  space.color = static_cast<tab_groups::TabGroupColorId>(spaces.size() % 8);

  if (spaces.empty()) {
    space.is_active = true;
  }

  const std::string new_id = space.id;
  spaces.push_back(std::move(space));
  SaveSpaces(spaces);
  NotifySpacesChanged();
  return new_id;
}

void SpaceManager::RemoveSpace(const std::string& id) {
  RefreshCacheIfNeeded();
  auto spaces = cached_spaces_;

  if (spaces.size() <= 1) {
    return;
  }

  size_t removed_index = 0;
  bool was_active = false;
  for (size_t i = 0; i < spaces.size(); ++i) {
    if (spaces[i].id == id) {
      removed_index = i;
      was_active = spaces[i].is_active;
      break;
    }
  }

  std::erase_if(spaces, [&](const Space& s) { return s.id == id; });

  if (was_active && !spaces.empty()) {
    const size_t new_active_index =
        std::min(removed_index, spaces.size() - 1);
    for (auto& space : spaces) {
      space.is_active = false;
    }
    spaces[new_active_index].is_active = true;
  }

  // Renumber so order stays dense.
  for (size_t i = 0; i < spaces.size(); ++i) {
    spaces[i].order = static_cast<int>(i);
  }

  SaveSpaces(spaces);
  NotifySpacesChanged();

  if (was_active && !cached_spaces_.empty()) {
    for (const auto& space : cached_spaces_) {
      if (space.is_active) {
        NotifyActiveSpaceChanged(space.id);
        break;
      }
    }
  }
}

void SpaceManager::ActivateSpace(const std::string& id) {
  RefreshCacheIfNeeded();
  auto spaces = cached_spaces_;

  bool changed = false;
  bool found = false;
  for (auto& space : spaces) {
    const bool should_be_active = (space.id == id);
    if (should_be_active) {
      found = true;
    }
    if (space.is_active != should_be_active) {
      changed = true;
    }
    space.is_active = should_be_active;
  }

  if (!found || !changed) {
    return;
  }

  SaveSpaces(spaces);
  NotifySpacesChanged();
  NotifyActiveSpaceChanged(id);
}

void SpaceManager::ActivateAdjacentSpace(bool forward) {
  RefreshCacheIfNeeded();
  if (cached_spaces_.size() <= 1) {
    return;
  }

  size_t active_index = 0;
  for (size_t i = 0; i < cached_spaces_.size(); ++i) {
    if (cached_spaces_[i].is_active) {
      active_index = i;
      break;
    }
  }

  const size_t count = cached_spaces_.size();
  const size_t next_index =
      forward ? (active_index + 1) % count
              : (active_index + count - 1) % count;

  ActivateSpace(cached_spaces_[next_index].id);
}

void SpaceManager::RenameSpace(const std::string& id,
                                const std::string& new_name) {
  RefreshCacheIfNeeded();
  auto spaces = cached_spaces_;
  for (auto& space : spaces) {
    if (space.id == id) {
      space.name = new_name;
      break;
    }
  }
  SaveSpaces(spaces);
  NotifySpacesChanged();
}

void SpaceManager::SetSpaceIcon(const std::string& id,
                                 const std::string& icon) {
  RefreshCacheIfNeeded();
  auto spaces = cached_spaces_;
  for (auto& space : spaces) {
    if (space.id == id) {
      space.icon = icon;
      break;
    }
  }
  SaveSpaces(spaces);
  NotifySpacesChanged();
}

void SpaceManager::SetSpaceProfile(const std::string& id,
                                    const std::string& profile_id) {
  RefreshCacheIfNeeded();
  auto spaces = cached_spaces_;
  for (auto& space : spaces) {
    if (space.id == id) {
      space.profile_id = profile_id;
      break;
    }
  }
  SaveSpaces(spaces);
  NotifySpacesChanged();
}

void SpaceManager::ReorderSpace(const std::string& id, int new_index) {
  RefreshCacheIfNeeded();
  auto spaces = cached_spaces_;

  auto it = std::find_if(spaces.begin(), spaces.end(),
                         [&id](const Space& s) { return s.id == id; });
  if (it == spaces.end()) {
    return;
  }

  Space moved = *it;
  spaces.erase(it);

  const int clamped = std::clamp(new_index, 0,
                                 static_cast<int>(spaces.size()));
  spaces.insert(spaces.begin() + clamped, std::move(moved));

  for (size_t i = 0; i < spaces.size(); ++i) {
    spaces[i].order = static_cast<int>(i);
  }

  SaveSpaces(spaces);
  NotifySpacesChanged();
}

// ── Persistence ─────────────────────────────────────────────────────────────

void SpaceManager::SaveSpaces(const std::vector<Space>& spaces) {
  std::vector<Space> ordered = spaces;
  SortByOrder(ordered);

  if (pref_available_) {
    base::ListValue list;
    for (const auto& space : ordered) {
      list.Append(space.ToDict());
    }
    writing_ = true;
    pref_service_->SetList(kSpacesPref, std::move(list));
    writing_ = false;
  }
  cached_spaces_ = std::move(ordered);
  cache_dirty_ = false;

  for (const auto& space : cached_spaces_) {
    if (space.is_active) {
      last_known_active_id_ = space.id;
      break;
    }
  }
}

}  // namespace avora
