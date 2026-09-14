// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_search_engine.h"

#include "base/strings/escape.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/prefs/pref_registry_simple.h"

namespace avora {

SearchEngineManager::SearchEngineManager(PrefService* prefs)
    : prefs_(prefs) {}

SearchEngineManager::~SearchEngineManager() = default;

// static
void SearchEngineManager::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  // Placeholder for search engine preference registration.
}

std::string SearchEngineManager::BuildSearchURL(
    const std::string& query) const {
  std::string escaped =
      base::EscapeQueryParamValue(query, true);
  return "https://www.google.com/search?q=" + escaped;
}

}  // namespace avora
