// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_tab_site_instance.h"

#include "chrome/browser/avora/avora_profile.h"
#include "chrome/browser/avora/avora_sidebar_item.h"
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

// The identity |space_id| browses as.  An unknown Space, or an empty id, falls
// back to the global active Space, and a Space whose profile is gone to the
// default profile.  Null only when there is no Space at all.
const BrowserProfile* ProfileForSpace(PrefService* prefs,
                                      const std::string& space_id,
                                      BrowserProfileStore& store) {
  SpaceManager space_manager(prefs);
  const Space* space = space_id.empty()
                           ? space_manager.GetActiveSpace()
                           : space_manager.GetSpaceById(space_id);
  if (!space) {
    space = space_manager.GetActiveSpace();
  }
  if (!space) {
    return nullptr;
  }
  const BrowserProfile* browser_profile =
      store.GetProfileById(space->profile_id);
  return browser_profile ? browser_profile : store.GetDefaultProfile();
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

  BrowserProfileStore store(prefs);
  const BrowserProfile* browser_profile =
      ProfileForSpace(prefs, space_id, store);
  if (!browser_profile || UsesDefaultPartition(*browser_profile)) {
    return tab_util::GetSiteInstanceForNewTab(profile, url);
  }

  return CreateSiteInstanceForProfile(profile, *browser_profile, url);
}

scoped_refptr<content::SiteInstance> GetSiteInstanceForRestoredAvoraTab(
    BrowserWindowInterface* browser,
    const std::string& tab_guid,
    const GURL& url) {
  Profile* profile = browser ? browser->GetProfile() : nullptr;
  PrefService* prefs = profile ? profile->GetPrefs() : nullptr;
  if (!prefs) {
    return nullptr;
  }

  std::string space_id;
  if (!tab_guid.empty()) {
    const SidebarItemStore item_store(prefs);
    const SidebarItem* state = item_store.GetItemById(tab_guid);
    if (state && state->type == SidebarItemType::kTabState) {
      space_id = state->space_id;
    }
  }
  if (space_id.empty()) {
    space_id = ActiveSpaceIdForWindow(browser);
  }

  BrowserProfileStore store(prefs);
  const BrowserProfile* browser_profile =
      ProfileForSpace(prefs, space_id, store);
  if (!browser_profile) {
    return nullptr;
  }
  // Null for the default partition, which keeps the stock restore path.
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
