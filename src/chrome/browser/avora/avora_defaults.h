// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_DEFAULTS_H_
#define CHROME_BROWSER_AVORA_AVORA_DEFAULTS_H_

class PrefService;

namespace avora {

// Chromium's built-in default for the vertical tab strip uncollapsed width.
inline constexpr int kChromiumDefaultSidebarWidth = 240;

// Avora's preferred default — wider so tab titles are readable at launch.
inline constexpr int kAvoraDefaultSidebarWidth = 300;

// Applies Avora-specific preference overrides.  Safe to call multiple times;
// only modifies prefs that are still at Chromium's stock defaults.
void ApplyBrowserDefaults(PrefService* prefs);

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_DEFAULTS_H_
