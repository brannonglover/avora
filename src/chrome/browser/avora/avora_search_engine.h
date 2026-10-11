// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_SEARCH_ENGINE_H_
#define CHROME_BROWSER_AVORA_AVORA_SEARCH_ENGINE_H_

#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "url/gurl.h"

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

// Resolves what the user typed into an Avora address field -- the sidebar
// chip, the mini window's address bar -- into a destination.  Input that looks
// like a URL is fixed up into one; anything else becomes a search on the
// profile's search engine, so the two surfaces can never disagree about
// whether "localhost:8080" is a place or a query.
//
// Returns an invalid GURL only for empty input.
GURL ResolveAddressInput(PrefService* prefs, std::u16string_view text);

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_SEARCH_ENGINE_H_
