// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_space_theme.h"

#include <algorithm>
#include <optional>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "ui/gfx/color_utils.h"

namespace avora {

namespace {

// The format version this build writes.
constexpr int kVersion = 1;

constexpr SpaceThemeMood kMoods[] = {
    {"dusk", "Dusk", SpaceTheme::Mode::kDark, {"#8C7CFF", "#FF7FA8"}},
    {"tide", "Tide", SpaceTheme::Mode::kDark, {"#4FB3FF", "#4FE0C0"}},
    {"ember", "Ember", SpaceTheme::Mode::kDark, {"#FF8A4C", "#FFC54D"}},
    {"arcade", "Arcade", SpaceTheme::Mode::kDark, {"#3DE8FF", "#FF4FD8"}},
    {"graphite", "Graphite", SpaceTheme::Mode::kDark, {"#9AA7B8", ""}},
    {"matcha", "Matcha", SpaceTheme::Mode::kLight, {"#7DBF5A", ""}},
    {"paper", "Paper", SpaceTheme::Mode::kLight, {"#C2A27C", ""}},
    {"blossom", "Blossom", SpaceTheme::Mode::kLight, {"#FF7FB0", "#B48CFF"}},
};

// Lightness of the colours on the studio's pad.
constexpr double kPadLightnessDark = 0.68;
constexpr double kPadLightnessLight = 0.52;

// The chrome is far quieter than the colour picked: this much of the picked
// colour's saturation survives into the sidebar, capped below.
constexpr double kChromeSaturationScale = 0.45;
constexpr double kChromeSaturationCap = 0.5;

// "#RRGGBB" only: themes never carry alpha.
std::optional<SkColor> ParseHex(std::string_view hex) {
  uint32_t value = 0;
  if (hex.size() != 7 || hex[0] != '#' ||
      !base::HexStringToUInt(hex.substr(1), &value)) {
    return std::nullopt;
  }
  return SkColorSetA(value, SK_AlphaOPAQUE);
}

// |color|'s hue at |lightness|, with its saturation turned down for chrome.
SkColor Shade(SkColor color, double lightness) {
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(color, &hsl);
  hsl.s = std::min(hsl.s * kChromeSaturationScale, kChromeSaturationCap);
  hsl.l = lightness;
  return color_utils::HSLToSkColor(hsl, SK_AlphaOPAQUE);
}

// Whether |text| reads well on |palette|: 7:1 on the sidebar, which carries
// the most text, and 4.5:1 on everything set into it.
bool IsReadable(SkColor text, const SpaceThemePalette& palette) {
  for (SkColor background : {palette.sidebar_top, palette.sidebar_bottom}) {
    if (color_utils::GetContrastRatio(text, background) < 7.0f) {
      return false;
    }
  }
  for (SkColor background : {palette.surface, palette.raised,
                             palette.tab_active, palette.tab_hover,
                             palette.tab_selected}) {
    if (color_utils::GetContrastRatio(text, background) < 4.5f) {
      return false;
    }
  }
  return true;
}

// Text in |color|'s hue, but only just, so it reads as white or ink; pushed
// towards pure white or black until it is readable on |palette|.
SkColor ReadableText(SkColor color, const SpaceThemePalette& palette) {
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(color, &hsl);
  const bool dark = palette.dark;
  hsl.s = dark ? 0.25 : 0.3;
  hsl.l = dark ? 0.95 : 0.13;
  SkColor text = color_utils::HSLToSkColor(hsl, SK_AlphaOPAQUE);
  for (int step = 0; step < 20 && !IsReadable(text, palette); ++step) {
    hsl.l = std::clamp(hsl.l + (dark ? 0.02 : -0.02), 0.0, 1.0);
    hsl.s *= 0.85;
    text = color_utils::HSLToSkColor(hsl, SK_AlphaOPAQUE);
  }
  return text;
}

}  // namespace

std::string SpaceTheme::Serialize() const {
  if (empty()) {
    return std::string();
  }
  base::ListValue list;
  for (size_t i = 0; i < colors.size() && i < kMaxSpaceThemeColors; ++i) {
    list.Append(SpaceThemeColorHex(colors[i]));
  }
  base::DictValue dict;
  dict.Set("v", kVersion);
  dict.Set("colors", std::move(list));
  dict.Set("mode", mode == Mode::kLight ? "light" : "dark");
  if (!mood.empty()) {
    dict.Set("mood", mood);
  }
  // DictValue keeps its keys sorted, so the output is canonical.
  return base::WriteJson(dict).value_or(std::string());
}

// static
SpaceTheme SpaceTheme::Parse(std::string_view serialized) {
  SpaceTheme theme;
  if (serialized.empty()) {
    return theme;
  }
  std::optional<base::DictValue> dict = base::JSONReader::ReadDict(
      serialized, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!dict) {
    return theme;
  }
  // Every version so far keeps "colors" and "mode" meaning what they mean
  // here; a version that changes them must change the field names too.
  if (std::optional<int> version = dict->FindInt("v");
      !version || *version < 1) {
    return theme;
  }
  const base::ListValue* colors = dict->FindList("colors");
  if (!colors) {
    return theme;
  }
  for (const base::Value& value : *colors) {
    if (theme.colors.size() == kMaxSpaceThemeColors) {
      break;
    }
    const std::string* hex = value.GetIfString();
    std::optional<SkColor> color = hex ? ParseHex(*hex) : std::nullopt;
    if (!color) {
      theme.colors.clear();
      return theme;
    }
    theme.colors.push_back(*color);
  }
  if (const std::string* mode = dict->FindString("mode")) {
    theme.mode = *mode == "light" ? Mode::kLight : Mode::kDark;
  }
  if (const std::string* mood = dict->FindString("mood")) {
    theme.mood = *mood;
  }
  if (theme.empty()) {
    theme.mood.clear();
  }
  return theme;
}

SpaceTheme SpaceThemeMood::ToTheme() const {
  SpaceTheme theme;
  theme.mode = mode;
  theme.mood = std::string(id);
  for (std::string_view hex : colors) {
    if (std::optional<SkColor> color = ParseHex(hex)) {
      theme.colors.push_back(*color);
    }
  }
  return theme;
}

base::span<const SpaceThemeMood> GetSpaceThemeMoods() {
  return kMoods;
}

SkColor SpaceThemePalette::SidebarAt(float fraction) const {
  return color_utils::AlphaBlend(sidebar_bottom, sidebar_top,
                                 std::clamp(fraction, 0.0f, 1.0f));
}

SpaceThemePalette DefaultSpaceThemePalette() {
  // The graphite look the sidebar views were drawn with before themes.
  constexpr SkColor kText = SkColorSetRGB(0xED, 0xF2, 0xF5);
  SpaceThemePalette palette;
  palette.dark = true;
  palette.sidebar_top = SkColorSetRGB(0x18, 0x1C, 0x20);
  palette.sidebar_bottom = palette.sidebar_top;
  palette.surface = SkColorSetRGB(0x22, 0x28, 0x2E);
  palette.raised = SkColorSetRGB(0x32, 0x38, 0x40);
  palette.tab_active = SkColorSetRGB(0x30, 0x36, 0x3E);
  palette.tab_hover = SkColorSetRGB(0x38, 0x3E, 0x48);
  palette.tab_selected = SkColorSetRGB(0x20, 0x24, 0x2A);
  palette.text = kText;
  palette.text_dim = SkColorSetA(kText, 0xB0);
  palette.text_faint = SkColorSetA(kText, 0x88);
  palette.hairline = SkColorSetA(kText, 0x30);
  palette.wash = SK_ColorWHITE;
  return palette;
}

SpaceThemePalette PaletteForSpaceTheme(const SpaceTheme& theme) {
  if (theme.empty()) {
    return DefaultSpaceThemePalette();
  }
  const SkColor top = theme.colors.front();
  const SkColor bottom = theme.colors.back();
  const bool dark = theme.mode == SpaceTheme::Mode::kDark;

  SpaceThemePalette palette;
  palette.dark = dark;
  if (dark) {
    palette.sidebar_top = Shade(top, 0.12);
    palette.sidebar_bottom = Shade(bottom, 0.12);
    palette.surface = Shade(top, 0.155);
    palette.raised = Shade(top, 0.215);
    palette.tab_active = Shade(top, 0.205);
    palette.tab_hover = Shade(top, 0.235);
    palette.tab_selected = Shade(top, 0.15);
  } else {
    palette.sidebar_top = Shade(top, 0.915);
    palette.sidebar_bottom = Shade(bottom, 0.915);
    palette.surface = Shade(top, 0.965);
    palette.raised = Shade(top, 0.86);
    palette.tab_active = Shade(top, 0.985);
    palette.tab_hover = Shade(top, 0.875);
    palette.tab_selected = Shade(top, 0.89);
  }

  palette.text = ReadableText(top, palette);
  palette.text_dim = SkColorSetA(palette.text, 0xB0);
  palette.text_faint = SkColorSetA(palette.text, 0x88);
  palette.hairline = SkColorSetA(palette.text, dark ? 0x30 : 0x24);
  palette.wash = dark ? SK_ColorWHITE : SK_ColorBLACK;
  return palette;
}

SkColor SpaceThemePadColor(double hue, double vibrance, SpaceTheme::Mode mode) {
  color_utils::HSL hsl;
  hsl.h = std::clamp(hue, 0.0, 1.0);
  hsl.s = std::clamp(vibrance, 0.0, 1.0);
  hsl.l = mode == SpaceTheme::Mode::kLight ? kPadLightnessLight
                                           : kPadLightnessDark;
  return color_utils::HSLToSkColor(hsl, SK_AlphaOPAQUE);
}

std::string SpaceThemeColorHex(SkColor color) {
  return base::StringPrintf("#%02X%02X%02X", SkColorGetR(color),
                            SkColorGetG(color), SkColorGetB(color));
}

std::pair<double, double> SpaceThemePadPosition(SkColor color) {
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(color, &hsl);
  return {std::clamp(hsl.h, 0.0, 1.0), std::clamp(hsl.s, 0.0, 1.0)};
}

}  // namespace avora
