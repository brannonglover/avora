// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_search_engine.h"

#include "base/strings/escape.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/url_formatter/url_fixer.h"

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

GURL ResolveAddressInput(PrefService* prefs, std::u16string_view text) {
  const std::string input = base::UTF16ToUTF8(text);
  if (input.empty()) {
    return GURL();
  }

  // FixupURL aggressively prepends http:// and percent-encodes spaces, turning
  // plain search queries into bogus URLs.  These heuristics are what keep it
  // from doing that:
  //   - Contains a space            -> search  (URLs never have bare spaces)
  //   - Starts with a scheme        -> URL
  //   - Contains a dot and no space -> URL  (e.g. "example.com")
  //   - Single word like "localhost" or an IP -> URL
  //   - Everything else             -> search
  bool looks_like_url = false;
  if (input.find(' ') == std::string::npos) {
    if (input.find("://") != std::string::npos) {
      looks_like_url = true;
    } else if (input.find('.') != std::string::npos) {
      looks_like_url = true;
    } else if (input == "localhost" ||
               base::StartsWith(input, "localhost:",
                                base::CompareCase::INSENSITIVE_ASCII)) {
      looks_like_url = true;
    }
  }

  GURL url;
  if (looks_like_url) {
    url = url_formatter::FixupURL(input, std::string());
    if (!url.is_valid() ||
        (!url.SchemeIsHTTPOrHTTPS() && !url.SchemeIs("chrome"))) {
      looks_like_url = false;
    }
  }

  if (!looks_like_url) {
    SearchEngineManager engine_mgr(prefs);
    url = GURL(engine_mgr.BuildSearchURL(input));
  }
  return url;
}

}  // namespace avora
