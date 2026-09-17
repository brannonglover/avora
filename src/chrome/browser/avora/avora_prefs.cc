// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_prefs.h"

#include "chrome/browser/avora/avora_favorites.h"
#include "chrome/browser/avora/avora_import_provenance.h"
#include "chrome/browser/avora/avora_imported_link_store.h"
#include "chrome/browser/avora/avora_live_folder_credentials.h"
#include "chrome/browser/avora/avora_live_folder_store.h"
#include "chrome/browser/avora/avora_pinned_folders.h"
#include "chrome/browser/avora/avora_pinned_items.h"
#include "chrome/browser/avora/avora_profile.h"
#include "chrome/browser/avora/avora_search_engine.h"
#include "chrome/browser/avora/avora_sidebar_item.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace avora {

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  FavoritesManager::RegisterProfilePrefs(registry);
  ImportedLinkStore::RegisterProfilePrefs(registry);
  ImportProvenanceStore::RegisterProfilePrefs(registry);
  PinnedFoldersManager::RegisterProfilePrefs(registry);
  PinnedItemsManager::RegisterProfilePrefs(registry);
  SearchEngineManager::RegisterProfilePrefs(registry);

  BrowserProfileStore::RegisterProfilePrefs(registry);
  SpaceManager::RegisterProfilePrefs(registry);
  SidebarItemStore::RegisterProfilePrefs(registry);

  LiveFolderStore::RegisterProfilePrefs(registry);
  LiveFolderCredentialStore::RegisterProfilePrefs(registry);

  registry->RegisterIntegerPref(kTodayTabExpiryHoursPref,
                                kDefaultTodayTabExpiryHours);
  registry->RegisterBooleanPref(kImportOfferedPref, false);
}

base::TimeDelta GetTodayTabExpiry(PrefService* pref_service) {
  if (!pref_service || !pref_service->FindPreference(kTodayTabExpiryHoursPref)) {
    return base::Hours(kDefaultTodayTabExpiryHours);
  }
  const int hours = pref_service->GetInteger(kTodayTabExpiryHoursPref);
  return hours > 0 ? base::Hours(hours) : base::TimeDelta();
}

}  // namespace avora
