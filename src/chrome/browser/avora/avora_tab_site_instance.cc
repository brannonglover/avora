// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_tab_site_instance.h"

#include "chrome/browser/avora/avora_profile.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_storage_partition.h"
#include "chrome/browser/avora/avora_window_session_data.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/prefs/pref_service.h"

namespace avora {

namespace {

// Resolves the Space a new tab in |browser| belongs to.  Prefers the
// window-local Space recorded by BrowserView; falls back to the global active
// Space so surfaces without Avora views still behave sensibly.
std::string ActiveSpaceIdForWindow(BrowserWindowInterface* browser) {
  if (!browser) {
    return std::string();
  }
  if (const auto* data =
          AvoraWindowSessionData::Get(browser->GetUnownedUserDataHost())) {
    return data->active_space_id();
  }
  return std::string();
}

}  // namespace

scoped_refptr<content::SiteInstance> GetSiteInstanceForSpace(
    Profile* profile,
    const std::string& space_id,
    const GURL& url) {
  if (!profile) {
    return nullptr;
  }

  PrefService* prefs = profile->GetPrefs();
  if (!prefs) {
    return tab_util::GetSiteInstanceForNewTab(profile, url);
  }

  SpaceManager space_manager(prefs);

  // An explicit Space wins; otherwise fall back to the global active Space.
  const Space* space = space_id.empty()
                           ? space_manager.GetActiveSpace()
                           : space_manager.GetSpaceById(space_id);
  if (!space) {
    space = space_manager.GetActiveSpace();
  }
  if (!space) {
    return tab_util::GetSiteInstanceForNewTab(profile, url);
  }

  BrowserProfileStore store(prefs);
  const BrowserProfile* browser_profile =
      store.GetProfileById(space->profile_id);
  if (!browser_profile) {
    browser_profile = store.GetDefaultProfile();
  }
  if (!browser_profile || UsesDefaultPartition(*browser_profile)) {
    return tab_util::GetSiteInstanceForNewTab(profile, url);
  }

  return CreateSiteInstanceForProfile(profile, *browser_profile, url);
}

scoped_refptr<content::SiteInstance> GetSiteInstanceForNewAvoraTab(
    BrowserWindowInterface* browser,
    const GURL& url) {
  if (!browser) {
    return nullptr;
  }
  return GetSiteInstanceForSpace(browser->GetProfile(),
                                 ActiveSpaceIdForWindow(browser), url);
}

}  // namespace avora
