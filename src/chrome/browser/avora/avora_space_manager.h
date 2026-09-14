// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_SPACE_MANAGER_H_
#define CHROME_BROWSER_AVORA_AVORA_SPACE_MANAGER_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "chrome/browser/avora/avora_space.h"
#include "components/prefs/pref_change_registrar.h"

class PrefService;
class PrefRegistrySimple;

namespace avora {

// Observer interface so the UI layer can react to space changes
// without the manager knowing about views.
class SpaceManagerObserver : public base::CheckedObserver {
 public:
  // Called whenever the list of spaces changes (add / remove / reorder).
  virtual void OnSpacesChanged() {}

  // Called when the active space switches.
  virtual void OnActiveSpaceChanged(const std::string& space_id) {}
};

// Manages Avora Spaces -- workspaces that own organizational state.  A Space's
// sidebar contents live in SidebarItemStore; its browser identity comes from
// the BrowserProfile named by Space::profile_id.
//
// Instances are cheap and stateless beyond a read cache: all state lives in
// PrefService.  Any number of components may own their own SpaceManager for the
// same profile and they will stay consistent, because each instance watches the
// backing pref and re-notifies its observers when another instance writes.
// That means views can simply own one rather than hunting for a shared
// instance through the view hierarchy.
class SpaceManager {
 public:
  static constexpr char kSpacesPref[] = "avora.spaces";

  explicit SpaceManager(PrefService* pref_service);
  ~SpaceManager();

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  void AddObserver(SpaceManagerObserver* observer);
  void RemoveObserver(SpaceManagerObserver* observer);

  // All spaces, sorted by Space::order.
  std::vector<Space> GetSpaces() const;

  // The currently active space, or nullptr when there are none.
  const Space* GetActiveSpace() const;

  const Space* GetSpaceById(const std::string& id) const;

  // Spaces sharing a given browser identity, sorted by order.
  std::vector<Space> GetSpacesForProfile(const std::string& profile_id) const;

  // Creates a space and returns its id.  An empty |icon| picks a default; an
  // empty |profile_id| attaches the space to the default browser identity.
  std::string CreateSpace(const std::string& name,
                          const std::string& icon = std::string(),
                          const std::string& profile_id = std::string());

  // Removes a space.  The last remaining space cannot be removed.  Callers are
  // responsible for clearing that space's sidebar items.
  void RemoveSpace(const std::string& id);

  void ActivateSpace(const std::string& id);

  // Activates the space before/after the active one in |order|.  Wraps around.
  // These back the swipe and mouse back/forward gestures.
  void ActivateAdjacentSpace(bool forward);

  void RenameSpace(const std::string& id, const std::string& new_name);
  void SetSpaceIcon(const std::string& id, const std::string& icon);

  // Repoints a space at a different browser identity.  Existing tabs keep
  // their current partition until they are reloaded.
  void SetSpaceProfile(const std::string& id, const std::string& profile_id);

  // Moves |id| to position |new_index| and renumbers the rest.
  void ReorderSpace(const std::string& id, int new_index);

 private:
  void SaveSpaces(const std::vector<Space>& spaces);
  void RefreshCacheIfNeeded() const;
  void NotifySpacesChanged();
  void NotifyActiveSpaceChanged(const std::string& id);

  // Fired when another SpaceManager instance writes the backing pref.
  void OnSpacesPrefChanged();

  raw_ptr<PrefService> pref_service_;

  // True when the "avora.spaces" pref is registered with the PrefService.
  // When false all storage is in-memory only.
  bool pref_available_ = false;

  // Set while this instance is writing, so we don't re-notify for our own
  // change after having already notified synchronously.
  bool writing_ = false;

  // Tracks the active space so external pref changes can tell whether the
  // active space actually moved.
  std::string last_known_active_id_;

  mutable std::vector<Space> cached_spaces_;
  mutable bool cache_dirty_ = true;

  PrefChangeRegistrar pref_registrar_;

  base::ObserverList<SpaceManagerObserver> observers_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_SPACE_MANAGER_H_
