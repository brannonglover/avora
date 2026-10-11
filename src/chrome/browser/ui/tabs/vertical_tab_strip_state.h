// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_TABS_VERTICAL_TAB_STRIP_STATE_H_
#define CHROME_BROWSER_UI_TABS_VERTICAL_TAB_STRIP_STATE_H_

namespace tabs {

// Avora override: Chromium ships 240 here.  375 is wide enough for tab
// titles to be readable at launch.  This constant is the single source for
// the initial width -- it seeds the registered value of
// prefs::kVerticalTabsUncollapsedWidth, the default member below, and the
// layout code -- so changing it here reaches every window-creation path
// (new window, session restore with no stored width, and window clone).
inline constexpr int kVerticalTabStripDefaultUncollapsedWidth = 375;

// The width Chromium shipped as its default before Avora widened it.  A
// restored window carrying exactly this width was never sized by hand -- it
// just predates the Avora default -- so it is migrated forward rather than
// preserved.  See VerticalTabStripStateController's constructor.
inline constexpr int kChromiumLegacyDefaultUncollapsedWidth = 240;

// Per-window state for the vertical tab strip.
struct VerticalTabStripState {
  // Whether the vertical tab strip is collapsed.
  bool collapsed = false;
  // The width of the vertical tab strip when it is not collapsed.
  int uncollapsed_width = kVerticalTabStripDefaultUncollapsedWidth;
};

}  // namespace tabs

#endif  // CHROME_BROWSER_UI_TABS_VERTICAL_TAB_STRIP_STATE_H_
