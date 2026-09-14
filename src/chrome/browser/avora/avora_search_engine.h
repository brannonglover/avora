// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_SEARCH_ENGINE_H_
#define CHROME_BROWSER_AVORA_AVORA_SEARCH_ENGINE_H_

#include <string>

#include "base/memory/raw_ptr.h"

class PrefRegistrySimple;
class PrefService;

namespace avora {

// Simple search-engine manager that builds search URLs from user queries.
// Currently defaults to Google search.
class SearchEngineManager {
 public:
  explicit SearchEngineManager(PrefService* prefs);
  ~SearchEngineManager();

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  // Builds a full search URL for the given query string (UTF-8).
  std::string BuildSearchURL(const std::string& query) const;

 private:
  raw_ptr<PrefService> prefs_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_SEARCH_ENGINE_H_
