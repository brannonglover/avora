// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_DEFAULTS_H_
#define CHROME_BROWSER_AVORA_AVORA_DEFAULTS_H_

class PrefService;

namespace avora {

// Applies Avora-specific preference overrides.  Safe to call multiple times;
// only modifies prefs the user has not set themselves.
void ApplyBrowserDefaults(PrefService* pref_service);

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_DEFAULTS_H_
