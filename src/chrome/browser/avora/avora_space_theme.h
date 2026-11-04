// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_SPACE_THEME_H_
#define CHROME_BROWSER_AVORA_AVORA_SPACE_THEME_H_

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "third_party/skia/include/core/SkColor.h"

namespace avora {

// How a Space's chrome looks: the colours it is tinted with and whether it is
// light or dark.  A Space with no theme keeps Avora's default graphite look.
//
// A theme is shared across the account as `spaces.theme`, the string
// Serialize() returns, so the phone tints itself the same way.  The format is
// part of the sync contract (docs/SYNC.md in the avoraapp repository):
//
//   {"colors":["#8C7CFF","#FF7FA8"],"mode":"dark","mood":"dusk","v":1}
//
// Readers take what they understand and ignore the rest, so a later version
// can add fields without breaking older builds.
struct SpaceTheme {
  enum class Mode { kDark, kLight };

  // One colour tints the chrome evenly; two run it top to bottom as a
  // gradient.  These are the colours the user picked, vivid as picked: the
  // palette below works out the quieter shades the chrome is painted with.
  std::vector<SkColor> colors;

  Mode mode = Mode::kDark;

  // The mood the theme started from, e.g. "dusk", or empty.  Only the studio
  // reads it, to show which mood card is selected.
  std::string mood;

  bool empty() const { return colors.empty(); }

  // "" for an empty theme, else the canonical JSON above.  Equal themes give
  // equal strings, which is what lets sync diff the column as text.
  std::string Serialize() const;

  // Tolerates anything: unparseable input, an unknown version or bad colours
  // give an empty theme rather than an error.
  static SpaceTheme Parse(std::string_view serialized);

  bool operator==(const SpaceTheme&) const = default;
};

// The most colours a theme carries.
inline constexpr size_t kMaxSpaceThemeColors = 2;

// A ready-made starting point in the Theme Studio.
struct SpaceThemeMood {
  std::string_view id;
  std::string_view name;
  SpaceTheme::Mode mode;
  // "#RRGGBB"; one or two of them.
  std::string_view colors[kMaxSpaceThemeColors];

  SpaceTheme ToTheme() const;
};

// The moods, in studio order.
base::span<const SpaceThemeMood> GetSpaceThemeMoods();

// Every colour Avora paints a window's chrome with.
struct SpaceThemePalette {
  bool dark = true;

  // The sidebar and frame.  Equal unless the theme is a gradient.
  SkColor sidebar_top;
  SkColor sidebar_bottom;

  // Popovers and menus that open from the sidebar.
  SkColor surface;
  // Chips and fields set into the sidebar, e.g. the address chip.
  SkColor raised;

  SkColor tab_active;
  SkColor tab_hover;
  SkColor tab_selected;

  SkColor text;
  SkColor text_dim;
  SkColor text_faint;
  // Washes and dividers drawn over any of the above.
  SkColor hairline;
  SkColor wash;

  // The sidebar colour |fraction| of the way down, 0 being the top.
  SkColor SidebarAt(float fraction) const;
};

// The palette Avora has always used, for Spaces with no theme.
SpaceThemePalette DefaultSpaceThemePalette();

// Works out the palette for |theme|.  Text keeps at least 7:1 contrast with
// the sidebar and 4.5:1 with every tab state, whatever colours were picked.
SpaceThemePalette PaletteForSpaceTheme(const SpaceTheme& theme);

// The Theme Studio's colour pad: hue across, vibrance up.  The pad holds
// lightness fixed per mode so every spot on it reads well as an accent.
SkColor SpaceThemePadColor(double hue, double vibrance, SpaceTheme::Mode mode);
// |color| as "#RRGGBB", the form themes and accents are stored in.
std::string SpaceThemeColorHex(SkColor color);

// Where |color| sits on the pad: {hue, vibrance}, each in [0, 1].
std::pair<double, double> SpaceThemePadPosition(SkColor color);

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_SPACE_THEME_H_
