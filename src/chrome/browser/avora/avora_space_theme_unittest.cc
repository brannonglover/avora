// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_space_theme.h"

#include <set>
#include <string>

#include "base/values.h"
#include "chrome/browser/avora/avora_space.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/color_utils.h"

namespace avora {
namespace {

SpaceTheme Theme(std::initializer_list<SkColor> colors,
                 SpaceTheme::Mode mode = SpaceTheme::Mode::kDark) {
  SpaceTheme theme;
  theme.colors = colors;
  theme.mode = mode;
  return theme;
}

TEST(AvoraSpaceThemeTest, EmptyThemeSerializesToNothing) {
  EXPECT_EQ(SpaceTheme().Serialize(), "");
  EXPECT_TRUE(SpaceTheme::Parse("").empty());
}

TEST(AvoraSpaceThemeTest, SerializesCanonically) {
  SpaceTheme theme = Theme({SkColorSetRGB(0x8C, 0x7C, 0xFF),
                            SkColorSetRGB(0xFF, 0x7F, 0xA8)});
  theme.mood = "dusk";
  // The sync contract in avoraapp's docs/SYNC.md quotes this string.
  EXPECT_EQ(theme.Serialize(),
            R"({"colors":["#8C7CFF","#FF7FA8"],"mode":"dark","mood":"dusk",)"
            R"("v":1})");
}

TEST(AvoraSpaceThemeTest, RoundTrips) {
  SpaceTheme theme =
      Theme({SkColorSetRGB(0x7D, 0xBF, 0x5A)}, SpaceTheme::Mode::kLight);
  EXPECT_EQ(SpaceTheme::Parse(theme.Serialize()), theme);
}

TEST(AvoraSpaceThemeTest, ParseIgnoresWhatItDoesNotUnderstand) {
  // A later version may add fields; the colours and mode still apply.
  const SpaceTheme theme = SpaceTheme::Parse(
      R"({"v":2,"colors":["#4FB3FF"],"mode":"light","grain":0.3})");
  EXPECT_EQ(theme, Theme({SkColorSetRGB(0x4F, 0xB3, 0xFF)},
                         SpaceTheme::Mode::kLight));
}

TEST(AvoraSpaceThemeTest, ParseKeepsAtMostTwoColors) {
  const SpaceTheme theme = SpaceTheme::Parse(
      R"({"v":1,"colors":["#111111","#222222","#333333"]})");
  EXPECT_EQ(theme.colors.size(), kMaxSpaceThemeColors);
}

TEST(AvoraSpaceThemeTest, ParseRejectsBadInput) {
  for (const char* input :
       {"not json", "[]", R"({"colors":["#4FB3FF"]})",
        R"({"v":1,"colors":[]})", R"({"v":1,"colors":["blue"]})",
        R"({"v":1,"colors":["#4FB3FF",7]})",
        R"({"v":1,"colors":["#804FB3FF"]})", R"({"v":0,"colors":["#4FB3FF"]})"}) {
    EXPECT_TRUE(SpaceTheme::Parse(input).empty()) << input;
  }
}

TEST(AvoraSpaceThemeTest, SpaceStoresItsTheme) {
  Space space;
  space.id = "s1";
  space.name = "Work";
  space.theme = GetSpaceThemeMoods().front().ToTheme();
  EXPECT_EQ(Space::FromDict(space.ToDict()).theme, space.theme);

  // Spaces written before themes have none.
  base::DictValue older = space.ToDict();
  older.Remove("theme");
  EXPECT_TRUE(Space::FromDict(older).theme.empty());
}

TEST(AvoraSpaceThemeTest, MoodsAreUniqueAndValid) {
  std::set<std::string_view> ids;
  for (const SpaceThemeMood& mood : GetSpaceThemeMoods()) {
    EXPECT_TRUE(ids.insert(mood.id).second) << mood.id;
    EXPECT_FALSE(mood.name.empty()) << mood.id;
    const SpaceTheme theme = mood.ToTheme();
    EXPECT_FALSE(theme.empty()) << mood.id;
    EXPECT_EQ(theme.mood, mood.id);
    EXPECT_EQ(SpaceTheme::Parse(theme.Serialize()), theme) << mood.id;
  }
}

TEST(AvoraSpaceThemeTest, NoThemeKeepsTheDefaultLook) {
  const SpaceThemePalette palette = PaletteForSpaceTheme(SpaceTheme());
  EXPECT_EQ(palette.sidebar_top, SkColorSetRGB(0x18, 0x1C, 0x20));
  EXPECT_EQ(palette.sidebar_top, palette.sidebar_bottom);
  EXPECT_TRUE(palette.dark);
}

TEST(AvoraSpaceThemeTest, GradientRunsTopToBottom) {
  const SpaceThemePalette palette = PaletteForSpaceTheme(
      Theme({SkColorSetRGB(0x3D, 0xE8, 0xFF), SkColorSetRGB(0xFF, 0x4F, 0xD8)}));
  EXPECT_NE(palette.sidebar_top, palette.sidebar_bottom);
  EXPECT_EQ(palette.SidebarAt(0), palette.sidebar_top);
  EXPECT_EQ(palette.SidebarAt(1), palette.sidebar_bottom);
}

TEST(AvoraSpaceThemeTest, ModeDecidesLightness) {
  const SkColor color = SkColorSetRGB(0x7D, 0xBF, 0x5A);
  EXPECT_TRUE(color_utils::IsDark(
      PaletteForSpaceTheme(Theme({color})).sidebar_top));
  EXPECT_FALSE(color_utils::IsDark(
      PaletteForSpaceTheme(Theme({color}, SpaceTheme::Mode::kLight))
          .sidebar_top));
}

// Whatever lands on the pad -- or in a synced theme -- text must stay
// readable on every surface the palette paints.
TEST(AvoraSpaceThemeTest, TextIsAlwaysReadable) {
  std::vector<SkColor> colors = {SK_ColorBLACK, SK_ColorWHITE,
                                 SkColorSetRGB(0xFF, 0xFF, 0x00),
                                 SkColorSetRGB(0x00, 0x00, 0xFF)};
  for (int hue = 0; hue < 12; ++hue) {
    for (double vibrance : {0.0, 0.5, 1.0}) {
      for (auto mode : {SpaceTheme::Mode::kDark, SpaceTheme::Mode::kLight}) {
        colors.push_back(SpaceThemePadColor(hue / 12.0, vibrance, mode));
      }
    }
  }
  for (SkColor top : colors) {
    for (auto mode : {SpaceTheme::Mode::kDark, SpaceTheme::Mode::kLight}) {
      const SpaceThemePalette p =
          PaletteForSpaceTheme(Theme({top, colors.front()}, mode));
      const std::string where = SpaceThemeColorHex(top);
      EXPECT_GE(color_utils::GetContrastRatio(p.text, p.sidebar_top), 7.0f)
          << where;
      EXPECT_GE(color_utils::GetContrastRatio(p.text, p.sidebar_bottom), 7.0f)
          << where;
      for (SkColor surface : {p.surface, p.raised, p.tab_active, p.tab_hover,
                              p.tab_selected}) {
        EXPECT_GE(color_utils::GetContrastRatio(p.text, surface), 4.5f)
            << where;
      }
    }
  }
}

TEST(AvoraSpaceThemeTest, PadPositionFindsThePickedColor) {
  const SkColor color =
      SpaceThemePadColor(0.6, 0.8, SpaceTheme::Mode::kDark);
  const auto [hue, vibrance] = SpaceThemePadPosition(color);
  EXPECT_NEAR(hue, 0.6, 0.01);
  EXPECT_NEAR(vibrance, 0.8, 0.02);
}

}  // namespace
}  // namespace avora
