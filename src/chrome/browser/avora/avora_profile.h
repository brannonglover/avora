// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_PROFILE_H_
#define CHROME_BROWSER_AVORA_AVORA_PROFILE_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/values.h"
#include "components/prefs/pref_change_registrar.h"

class PrefService;
class PrefRegistrySimple;

namespace avora {

// An Avora "Profile" is a browser *identity*: it scopes cookies, localStorage,
// IndexedDB, service workers, and permissions.  It deliberately does NOT own
// any organizational state -- that all belongs to a Space.
//
// Exactly one Space owns each BrowserProfile -- SpaceManager mints one per
// Space and retires it with the Space.  The default profile is the exception:
// it is adopted by the first Space and doubles as the fallback whenever a
// Space's identity cannot be resolved, so it is never removed.
//
// NOTE: named BrowserProfile rather than Profile to avoid shadowing Chromium's
// global ::Profile class, which is a different concept entirely.
struct BrowserProfile {
  std::string id;
  std::string name;

  // Storage partition name used to isolate this identity.  Empty means the
  // profile uses Chromium's default partition (see avora_storage_partition).
  std::string partition;

  base::DictValue ToDict() const;
  static BrowserProfile FromDict(const base::DictValue& dict);
};

// Persists the set of Avora browser identities.
class BrowserProfileStore {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnBrowserProfilesChanged() {}
  };

  static constexpr char kProfilesPref[] = "avora.profiles";

  // Stable id of the profile seeded on first run.  This one intentionally maps
  // to Chromium's default storage partition so that a fresh install keeps any
  // pre-existing cookies and logins.
  static constexpr char kDefaultProfileId[] = "default";

  explicit BrowserProfileStore(PrefService* pref_service);
  ~BrowserProfileStore();

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  std::vector<BrowserProfile> GetProfiles() const;

  // Returns the profile with |id|, or nullptr.
  const BrowserProfile* GetProfileById(const std::string& id) const;

  // Returns the default profile, creating it if it somehow went missing.
  const BrowserProfile* GetDefaultProfile() const;

  // Creates a new identity with its own storage partition.  Returns its id.
  std::string CreateProfile(const std::string& name);

  // Removes an identity.  The default profile cannot be removed.
  void RemoveProfile(const std::string& id);

  void RenameProfile(const std::string& id, const std::string& new_name);

 private:
  void Save(const std::vector<BrowserProfile>& profiles);
  void RefreshCacheIfNeeded() const;
  void NotifyChanged();
  void OnProfilesPrefChanged();

  raw_ptr<PrefService> pref_service_;
  bool pref_available_ = false;
  bool writing_ = false;

  mutable std::vector<BrowserProfile> cached_;
  mutable bool cache_dirty_ = true;

  PrefChangeRegistrar pref_registrar_;
  base::ObserverList<Observer> observers_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_PROFILE_H_
