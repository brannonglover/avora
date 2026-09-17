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
// Spaces and identities are one-to-one, and this class is what keeps them so:
// creating a Space mints its identity, deleting one retires it, and renaming
// one renames it.  Only the first Space on an install is special -- it adopts
// the default identity so a pre-Spaces install keeps its cookies and logins.
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

  // Creates a space and returns its id.  An empty |icon| or |accent_color|
  // picks a default.
  //
  // An empty |profile_id| -- the normal case -- mints a fresh identity for the
  // space, so Spaces and identities stay one-to-one.  Passing an explicit
  // |profile_id| is for restoring a space that already has one; it deliberately
  // bypasses that invariant and no new identity is created.
  std::string CreateSpace(const std::string& name,
                          const std::string& icon = std::string(),
                          const std::string& profile_id = std::string(),
                          const std::string& accent_color = std::string());

  // Removes a space and, with it, the identity the space owned.  The last
  // remaining space cannot be removed.  Callers are responsible for clearing
  // that space's sidebar items.
  //
  // NOTE: this drops the identity's pref entry, not its on-disk
  // StoragePartition.  Cookies already written to disk survive until that
  // partition is cleared separately.
  void RemoveSpace(const std::string& id);

  void ActivateSpace(const std::string& id);

  // Activates the space before/after the active one in |order|.  Wraps around.
  // These back the swipe and mouse back/forward gestures.
  void ActivateAdjacentSpace(bool forward);

  void RenameSpace(const std::string& id, const std::string& new_name);

  // |icon| is a Lucide identifier; unknown ones fall back to the default icon.
  void SetSpaceIcon(const std::string& id, const std::string& icon);

  // |accent_color| is "#RRGGBB".
  void SetSpaceAccentColor(const std::string& id,
                           const std::string& accent_color);

  // Applies everything the Space editor can change in a single write, so the
  // UI rebuilds once instead of three times.
  void UpdateSpace(const std::string& id,
                   const std::string& name,
                   const std::string& icon,
                   const std::string& accent_color);

  // Repoints a space at a different browser identity.  Existing tabs keep
  // their current partition until they are reloaded.
  //
  // Breaks the one-Space-one-identity invariant, so there is no UI for it:
  // this exists for data repair and for tests.
  void SetSpaceProfile(const std::string& id, const std::string& profile_id);

  // Moves |id| to position |new_index| and renumbers the rest.
  void ReorderSpace(const std::string& id, int new_index);

 private:
  // Rewrites Spaces stored by older builds -- emoji icons, tab-group accent
  // colours -- into the current representation, once, at startup.
  void MigrateStoredSpaces();

  // Repairs Spaces stored before the one-to-one rule, where several could
  // share the default identity.  Walks Spaces in order: the first one needing
  // an identity claims the default, keeping its existing cookies; every other
  // Space with a missing, dangling, or already-claimed identity is given its
  // own.  Idempotent, which matters because SpaceManager is constructed on
  // paths as hot as opening a tab.
  void BackfillSpaceIdentities();

  // Mints the identity a new Space owns.  |is_first_space| adopts the default
  // identity instead of creating one: its partition is Chromium's default, so
  // an install upgrading from a pre-Spaces build keeps its cookies and logins.
  std::string CreateIdentityForSpace(const std::string& name,
                                     bool is_first_space);

  // Keeps an owned identity's name in step with its Space's.  The default
  // identity is left alone: it is also the fallback for Spaces whose identity
  // has gone missing, so its name is not any one Space's to change.
  void RenameIdentityForSpace(const std::string& profile_id,
                              const std::string& new_name);

  // Drops |profile_id| once no Space in |remaining| still points at it.  The
  // emptiness check matters because Spaces written before the one-to-one rule
  // could share an identity.
  void ReleaseIdentity(const std::string& profile_id,
                       const std::vector<Space>& remaining);

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
