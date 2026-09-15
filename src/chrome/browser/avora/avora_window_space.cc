// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_window_space.h"

#include "base/uuid.h"

namespace avora {

// ---------- helpers ----------------------------------------------------------

std::string WindowSpaceState::DefaultActiveSpaceId() const {
  if (const Space* active = space_manager_.GetActiveSpace()) {
    return active->id;
  }
  const auto spaces = space_manager_.GetSpaces();
  return spaces.empty() ? std::string() : spaces[0].id;
}

void WindowSpaceState::Init() {
  space_manager_.AddObserver(this);
}

// ---------- constructors / destructor ----------------------------------------

WindowSpaceState::WindowSpaceState(PrefService* prefs)
    : space_manager_(prefs),
      window_guid_(base::Uuid::GenerateRandomV4().AsLowercaseString()) {
  active_space_id_ = DefaultActiveSpaceId();
  Init();
}

WindowSpaceState::WindowSpaceState(PrefService* prefs,
                                   const std::string& restored_window_guid,
                                   const std::string& restored_space_id)
    : space_manager_(prefs), window_guid_(restored_window_guid) {
  // Use the restored Space if it still exists; otherwise fall back.
  if (!restored_space_id.empty() &&
      space_manager_.GetSpaceById(restored_space_id)) {
    active_space_id_ = restored_space_id;
  } else {
    active_space_id_ = DefaultActiveSpaceId();
  }
  Init();
}

WindowSpaceState::~WindowSpaceState() {
  space_manager_.RemoveObserver(this);
}

// ---------- mutation ---------------------------------------------------------

void WindowSpaceState::SetActiveSpaceId(const std::string& id) {
  if (id == active_space_id_) {
    return;
  }
  // Verify the Space exists.
  if (!space_manager_.GetSpaceById(id)) {
    return;
  }
  active_space_id_ = id;
  for (auto& obs : observers_) {
    obs.OnWindowActiveSpaceChanged(id);
  }
}

void WindowSpaceState::ActivateAdjacentSpace(bool forward) {
  const auto spaces = space_manager_.GetSpaces();
  if (spaces.size() <= 1) {
    return;
  }

  size_t active_index = 0;
  for (size_t i = 0; i < spaces.size(); ++i) {
    if (spaces[i].id == active_space_id_) {
      active_index = i;
      break;
    }
  }

  const size_t count = spaces.size();
  const size_t next =
      forward ? (active_index + 1) % count
              : (active_index + count - 1) % count;
  SetActiveSpaceId(spaces[next].id);
}

// ---------- observers --------------------------------------------------------

void WindowSpaceState::AddObserver(Observer* obs) {
  observers_.AddObserver(obs);
}

void WindowSpaceState::RemoveObserver(Observer* obs) {
  observers_.RemoveObserver(obs);
}

// ---------- SpaceManagerObserver ---------------------------------------------

void WindowSpaceState::OnSpacesChanged() {
  // If the Space this window was showing has been deleted, fall back to the
  // first available Space.
  if (!space_manager_.GetSpaceById(active_space_id_)) {
    const auto spaces = space_manager_.GetSpaces();
    const std::string new_id = spaces.empty() ? std::string() : spaces[0].id;
    if (new_id != active_space_id_) {
      active_space_id_ = new_id;
      for (auto& obs : observers_) {
        obs.OnWindowActiveSpaceChanged(active_space_id_);
      }
    }
  }
}

}  // namespace avora
