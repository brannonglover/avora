// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/color/avora_color_mixer.h"

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_recipe.h"

namespace {

constexpr SkColor kAvoraSidebarBg = SkColorSetRGB(0x18, 0x1C, 0x20);
constexpr SkColor kAvoraSidebarText = SkColorSetRGB(0xED, 0xF2, 0xF5);

// Active tab: bold lift so users can immediately spot the current tab.
constexpr SkColor kAvoraActiveTabBg = SkColorSetRGB(0x30, 0x36, 0x3E);

// Hover: pronounced highlight when the pointer enters a tab.
constexpr SkColor kAvoraHoverBg = SkColorSetRGB(0x38, 0x3E, 0x48);

}  // namespace

void AddAvoraColorMixer(ui::ColorProvider* provider,
                        const ui::ColorProviderKey& key) {
  ui::ColorMixer& mixer = provider->AddMixer();

  // Frame: use the dark sidebar color so the window chrome is seamless.
  mixer[ui::kColorFrameActive] = {kAvoraSidebarBg};
  mixer[ui::kColorFrameInactive] = {kAvoraSidebarBg};

  // Toolbar background matches sidebar.
  mixer[kColorToolbar] = {kAvoraSidebarBg};
  mixer[kColorToolbarText] = {kAvoraSidebarText};
  mixer[kColorToolbarTextDefault] = {kAvoraSidebarText};
  mixer[kColorToolbarButtonIcon] = {kAvoraSidebarText};
  mixer[kColorToolbarButtonIconDefault] = {kAvoraSidebarText};
  mixer[kColorToolbarButtonIconHovered] = {SK_ColorWHITE};
  mixer[kColorToolbarButtonIconInactive] = {SkColorSetA(kAvoraSidebarText, 0x80)};
  mixer[kColorToolbarContentAreaSeparator] = {kAvoraSidebarBg};

  // Omnibox / location bar (hidden, but override in case).
  mixer[kColorLocationBarBackground] = {kAvoraSidebarBg};
  mixer[kColorOmniboxText] = {kAvoraSidebarText};

  // Bookmark bar inherits sidebar look.
  mixer[kColorBookmarkBarBackground] = {kAvoraSidebarBg};
  mixer[kColorBookmarkBarForeground] = {kAvoraSidebarText};

  // Active tab: lighter background so users can identify the current tab.
  mixer[kColorTabBackgroundActiveFrameActive] = {kAvoraActiveTabBg};
  mixer[kColorTabBackgroundActiveFrameInactive] = {kAvoraActiveTabBg};

  // Inactive tabs: blend into sidebar, no background.
  mixer[kColorTabBackgroundInactiveFrameActive] = {kAvoraSidebarBg};
  mixer[kColorTabBackgroundInactiveFrameInactive] = {kAvoraSidebarBg};

  // Hover: pronounced lift so users know which tab the pointer is over.
  mixer[kColorTabBackgroundInactiveHoverFrameActive] = {kAvoraHoverBg};
  mixer[kColorTabBackgroundInactiveHoverFrameInactive] = {kAvoraHoverBg};

  // Selected (multi-select): midway between active and sidebar.
  mixer[kColorTabBackgroundSelectedFrameActive] =
      {SkColorSetRGB(0x20, 0x24, 0x2A)};
  mixer[kColorTabBackgroundSelectedFrameInactive] =
      {SkColorSetRGB(0x20, 0x24, 0x2A)};
  mixer[kColorTabBackgroundSelectedHoverFrameActive] = {kAvoraHoverBg};
  mixer[kColorTabBackgroundSelectedHoverFrameInactive] = {kAvoraHoverBg};

  // Tab foreground: bright white for active, dimmed for inactive.
  mixer[kColorTabForegroundActiveFrameActive] = {SK_ColorWHITE};
  mixer[kColorTabForegroundActiveFrameInactive] = {SK_ColorWHITE};
  mixer[kColorTabForegroundInactiveFrameActive] =
      {SkColorSetA(kAvoraSidebarText, 0xCC)};
  mixer[kColorTabForegroundInactiveFrameInactive] =
      {SkColorSetA(kAvoraSidebarText, 0xA0)};

  // Suppress individual tab divider lines.
  mixer[kColorTabDividerFrameActive] = {SkColorSetA(kAvoraSidebarText, 0x40)};
  mixer[kColorTabDividerFrameInactive] = {SkColorSetA(kAvoraSidebarText, 0x30)};

  // Tab strip controls match sidebar.
  mixer[kColorTabStripControlButtonInkDrop] = {SK_ColorTRANSPARENT};
  mixer[kColorTabStripControlButtonInkDropRipple] = {SK_ColorTRANSPARENT};

  // New-tab button matches sidebar.
  mixer[kColorNewTabButtonBackgroundFrameActive] = {kAvoraSidebarBg};
  mixer[kColorNewTabButtonBackgroundFrameInactive] = {kAvoraSidebarBg};
  mixer[kColorNewTabButtonForegroundFrameActive] = {kAvoraSidebarText};
  mixer[kColorNewTabButtonForegroundFrameInactive] = {kAvoraSidebarText};

  // Vertical tab strip shadow and outline.
  mixer[kColorVerticalTabStripShadow] = {SkColorSetARGB(0x33, 0, 0, 0)};
  mixer[kColorNewTabButtonFocusRing] = {SkColorSetRGB(0x6E, 0xA1, 0xF7)};

  // Web-contents background: use the frame color so the solid-color layer
  // behind the rounded content card blends seamlessly with the dark chrome.
  // Websites supply their own CSS background, so the only visible change is
  // that page-transition flashes are dark instead of white.
  mixer[kColorWebContentsBackground] = {kAvoraSidebarBg};
}
