// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_tab_site_instance.h"

#include "chrome/browser/avora/avora_profile.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_storage_partition.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "components/prefs/pref_service.h"

namespace avora {

scoped_refptr<content::SiteInstance> GetSiteInstanceForNewAvoraTab(
    Profile* profile,
    const GURL& url) {
  if (!profile) {
    return nullptr;
  }

  PrefService* prefs = profile->GetPrefs();
  if (!prefs) {
    return tab_util::GetSiteInstanceForNewTab(profile, url);
  }

  SpaceManager space_manager(prefs);
  const Space* active = space_manager.GetActiveSpace();
  if (!active) {
    return tab_util::GetSiteInstanceForNewTab(profile, url);
  }

  BrowserProfileStore store(prefs);
  const BrowserProfile* browser_profile =
      store.GetProfileById(active->profile_id);
  if (!browser_profile) {
    browser_profile = store.GetDefaultProfile();
  }
  if (!browser_profile || UsesDefaultPartition(*browser_profile)) {
    return tab_util::GetSiteInstanceForNewTab(profile, url);
  }

  return CreateSiteInstanceForProfile(profile, *browser_profile, url);
}

}  // namespace avora
