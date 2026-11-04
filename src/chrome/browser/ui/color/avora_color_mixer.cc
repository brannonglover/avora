// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/color/avora_color_mixer.h"

#include <map>
#include <memory>
#include <string>

#include "base/no_destructor.h"
#include "chrome/browser/avora/avora_space_theme.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_manager.h"
#include "ui/color/color_recipe.h"

namespace {

// Paints Chromium's own chrome -- frame, toolbar, tabs -- in |palette|, so it
// matches the sidebar Avora draws itself.
void AddPaletteMixer(ui::ColorProvider* provider,
                     const avora::SpaceThemePalette& palette) {
  ui::ColorMixer& mixer = provider->AddMixer();
  const SkColor bg = palette.sidebar_top;
  const SkColor text = palette.text;
  const SkColor emphasis = palette.dark ? SK_ColorWHITE : SK_ColorBLACK;

  // Frame: use the sidebar color so the window chrome is seamless.
  mixer[ui::kColorFrameActive] = {bg};
  mixer[ui::kColorFrameInactive] = {bg};

  // Toolbar background matches sidebar.
  mixer[kColorToolbar] = {bg};
  mixer[kColorToolbarText] = {text};
  mixer[kColorToolbarTextDefault] = {text};
  mixer[kColorToolbarButtonIcon] = {text};
  mixer[kColorToolbarButtonIconDefault] = {text};
  mixer[kColorToolbarButtonIconHovered] = {emphasis};
  mixer[kColorToolbarButtonIconInactive] = {SkColorSetA(text, 0x80)};
  mixer[kColorToolbarContentAreaSeparator] = {bg};

  // Omnibox / location bar (hidden, but override in case).
  mixer[kColorLocationBarBackground] = {bg};
  mixer[kColorOmniboxText] = {text};

  // Bookmark bar inherits sidebar look.
  mixer[kColorBookmarkBarBackground] = {bg};
  mixer[kColorBookmarkBarForeground] = {text};

  // Active tab: lighter background so users can identify the current tab.
  mixer[kColorTabBackgroundActiveFrameActive] = {palette.tab_active};
  mixer[kColorTabBackgroundActiveFrameInactive] = {palette.tab_active};

  // Inactive tabs: blend into sidebar, no background.  Over a gradient no
  // one colour blends, so they go transparent and the gradient shows through.
  const SkColor inactive =
      palette.sidebar_top == palette.sidebar_bottom ? bg : SK_ColorTRANSPARENT;
  mixer[kColorTabBackgroundInactiveFrameActive] = {inactive};
  mixer[kColorTabBackgroundInactiveFrameInactive] = {inactive};

  // Hover: pronounced lift so users know which tab the pointer is over.
  mixer[kColorTabBackgroundInactiveHoverFrameActive] = {palette.tab_hover};
  mixer[kColorTabBackgroundInactiveHoverFrameInactive] = {palette.tab_hover};

  // Selected (multi-select): midway between active and sidebar.
  mixer[kColorTabBackgroundSelectedFrameActive] = {palette.tab_selected};
  mixer[kColorTabBackgroundSelectedFrameInactive] = {palette.tab_selected};
  mixer[kColorTabBackgroundSelectedHoverFrameActive] = {palette.tab_hover};
  mixer[kColorTabBackgroundSelectedHoverFrameInactive] = {palette.tab_hover};

  // Tab foreground: full strength for active, dimmed for inactive.
  mixer[kColorTabForegroundActiveFrameActive] = {emphasis};
  mixer[kColorTabForegroundActiveFrameInactive] = {emphasis};
  mixer[kColorTabForegroundInactiveFrameActive] = {SkColorSetA(text, 0xCC)};
  mixer[kColorTabForegroundInactiveFrameInactive] = {SkColorSetA(text, 0xA0)};

  // Suppress individual tab divider lines.
  mixer[kColorTabDividerFrameActive] = {SkColorSetA(text, 0x40)};
  mixer[kColorTabDividerFrameInactive] = {SkColorSetA(text, 0x30)};

  // Tab strip controls match sidebar.
  mixer[kColorTabStripControlButtonInkDrop] = {SK_ColorTRANSPARENT};
  mixer[kColorTabStripControlButtonInkDropRipple] = {SK_ColorTRANSPARENT};

  // New-tab button matches sidebar.
  mixer[kColorNewTabButtonBackgroundFrameActive] = {bg};
  mixer[kColorNewTabButtonBackgroundFrameInactive] = {bg};
  mixer[kColorNewTabButtonForegroundFrameActive] = {text};
  mixer[kColorNewTabButtonForegroundFrameInactive] = {text};
  // The vertical strip's "New Tab" row (TabStripFlatEdgeButton) takes its
  // text and glyph from the tab search button's colours.
  mixer[kColorTabSearchButtonCRForegroundFrameActive] = {text};
  mixer[kColorTabSearchButtonCRForegroundFrameInactive] = {
      SkColorSetA(text, 0xCC)};

  // Vertical tab strip shadow and outline.
  mixer[kColorVerticalTabStripShadow] = {SkColorSetARGB(0x33, 0, 0, 0)};
  mixer[kColorNewTabButtonFocusRing] = {SkColorSetRGB(0x6E, 0xA1, 0xF7)};

  // Web-contents background: use the frame color so the solid-color layer
  // behind the rounded content card blends seamlessly with the chrome.
  // Websites supply their own CSS background, so the only visible change is
  // that page-transition flashes take the chrome's color instead of white.
  mixer[kColorWebContentsBackground] = {bg};
}

class SpaceThemeSupplier : public ui::ColorProviderKey::InitializerSupplier {
 public:
  explicit SpaceThemeSupplier(const avora::SpaceTheme& theme)
      : palette_(avora::PaletteForSpaceTheme(theme)) {}
  ~SpaceThemeSupplier() override = default;

  // ui::ColorProviderKey::InitializerSupplier:
  void AddColorMixers(ui::ColorProvider* provider,
                      const ui::ColorProviderKey& key) const override {
    AddPaletteMixer(provider, palette_);
  }

 private:
  const avora::SpaceThemePalette palette_;
};

// Suppliers by serialized theme.
std::map<std::string, std::unique_ptr<SpaceThemeSupplier>>& Suppliers() {
  static base::NoDestructor<
      std::map<std::string, std::unique_ptr<SpaceThemeSupplier>>>
      suppliers;
  return *suppliers;
}

}  // namespace

void AddAvoraColorMixer(ui::ColorProvider* provider,
                        const ui::ColorProviderKey& key) {
  AddPaletteMixer(provider, avora::DefaultSpaceThemePalette());
}

ui::ColorProviderKey::InitializerSupplier* GetAvoraSpaceThemeSupplier(
    const avora::SpaceTheme& theme) {
  if (theme.empty()) {
    return nullptr;
  }
  std::unique_ptr<SpaceThemeSupplier>& supplier =
      Suppliers()[theme.Serialize()];
  if (!supplier) {
    supplier = std::make_unique<SpaceThemeSupplier>(theme);
  }
  return supplier.get();
}

void ReleaseAvoraSpaceThemeSuppliers() {
  ui::ColorProviderManager::Get().ResetColorProviderCache();
  Suppliers().clear();
}
