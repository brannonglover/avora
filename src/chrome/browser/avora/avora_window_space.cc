// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_window_space.h"

#include <algorithm>

namespace avora {

WindowSpaceState::WindowSpaceState(PrefService* prefs)
    : space_manager_(prefs) {
  space_manager_.AddObserver(this);

  // Seed from the global pref so existing single-window sessions and new
  // windows start on whatever Space was last active.
  if (const Space* active = space_manager_.GetActiveSpace()) {
    active_space_id_ = active->id;
  }
}

WindowSpaceState::~WindowSpaceState() {
  space_manager_.RemoveObserver(this);
}

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

void WindowSpaceState::AddObserver(Observer* obs) {
  observers_.AddObserver(obs);
}

void WindowSpaceState::RemoveObserver(Observer* obs) {
  observers_.RemoveObserver(obs);
}

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
