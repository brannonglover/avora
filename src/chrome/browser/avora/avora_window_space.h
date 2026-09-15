// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_WINDOW_SPACE_H_
#define CHROME_BROWSER_AVORA_AVORA_WINDOW_SPACE_H_

#include <string>

#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "chrome/browser/avora/avora_space_manager.h"

class PrefService;

namespace avora {

// Per-window active Space state.
//
// Each browser window owns exactly one WindowSpaceState.  It holds the Space
// that *this* window is showing, independently of every other window.  The
// Space definitions themselves (name, icon, order, profile_id) remain shared
// and profile-scoped via SpaceManager; only the concept of "which Space is
// active" moves from the global pref to this per-window object.
//
// Observers are notified when this window's active Space changes.  Components
// that formerly derived the active Space from SpaceManager::GetActiveSpace()
// should instead query WindowSpaceState::active_space_id().
class WindowSpaceState : public SpaceManagerObserver {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // The active Space for this browser window has changed.
    virtual void OnWindowActiveSpaceChanged(const std::string& space_id) {}
  };

  // Construct for a brand-new window.  Seeds active_space_id_ from the global
  // pref (SpaceManager::GetActiveSpace), which acts as the default Space for
  // new windows.
  explicit WindowSpaceState(PrefService* prefs);

  // Construct for a restored window.  |restored_space_id| is the Space this
  // window was showing when the session was last saved.  If that Space no
  // longer exists, falls back to the global default.
  WindowSpaceState(PrefService* prefs,
                   const std::string& restored_window_guid,
                   const std::string& restored_space_id);

  ~WindowSpaceState() override;

  // The durable identity of this logical window, stable across restarts.
  const std::string& window_guid() const { return window_guid_; }

  // The Space this window is currently showing.
  const std::string& active_space_id() const { return active_space_id_; }

  // Switch this window to a different Space.  Fires
  // OnWindowActiveSpaceChanged on all observers.  Does NOT write is_active
  // to the global pref; the global pref's is_active field is retained only
  // for backward compatibility / new-window defaults.
  void SetActiveSpaceId(const std::string& id);

  // Advance to the next (forward=true) or previous Space in order.
  void ActivateAdjacentSpace(bool forward);

  // The shared Space-list manager.  Use for read-only queries: listing
  // Spaces, getting a Space by id, reading name/icon/order, CRUD operations
  // on the Space list itself.  Do NOT call ActivateSpace() on this.
  SpaceManager* space_manager() { return &space_manager_; }
  const SpaceManager* space_manager() const { return &space_manager_; }

  void AddObserver(Observer* obs);
  void RemoveObserver(Observer* obs);

  // SpaceManagerObserver — reacts to Space list changes (add/remove/reorder)
  // so that a deleted or reorganised active Space is handled gracefully.
  void OnSpacesChanged() override;

 private:
  // Shared init logic called by both constructors after space_manager_ and
  // active_space_id_ are set.
  void Init();

  // Choose the default active Space from the global pref.
  std::string DefaultActiveSpaceId() const;

  SpaceManager space_manager_;
  std::string window_guid_;
  std::string active_space_id_;
  base::ObserverList<Observer> observers_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_WINDOW_SPACE_H_
