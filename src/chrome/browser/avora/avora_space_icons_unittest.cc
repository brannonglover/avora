// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_space_icons.h"

#include <set>
#include <string>

#include "base/values.h"
#include "chrome/browser/avora/avora_space.h"
#include "components/tab_groups/tab_group_color.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace avora {
namespace {

TEST(AvoraSpaceIconsTest, CatalogCoversTheCommonSpaceTypes) {
  for (const char* id :
       {"briefcase", "user", "code", "palette", "shopping-cart", "plane",
        "graduation-cap", "wallet", "gamepad-2", "music", "book", "dumbbell",
        "house", "camera", "lightbulb"}) {
    EXPECT_NE(FindSpaceIcon(id), nullptr) << id;
  }
  EXPECT_NE(FindSpaceIcon(kDefaultSpaceIconId), nullptr);
}

TEST(AvoraSpaceIconsTest, CatalogEntriesAreUniqueAndUsable) {
  std::set<std::string_view> ids;
  for (const SpaceIcon& icon : GetSpaceIconCatalog()) {
    EXPECT_TRUE(ids.insert(icon.id).second) << icon.id;
    EXPECT_FALSE(icon.label.empty()) << icon.id;
    EXPECT_FALSE(icon.category.empty()) << icon.id;
    EXPECT_FALSE(icon.path_data.empty()) << icon.id;
  }
}

TEST(AvoraSpaceIconsTest, CatalogGroupsCategoriesTogether) {
  // The picker starts a new section whenever the category changes, so a
  // category must not reappear after another one.
  std::set<std::string_view> seen;
  std::string_view current;
  for (const SpaceIcon& icon : GetSpaceIconCatalog()) {
    if (icon.category != current) {
      EXPECT_TRUE(seen.insert(icon.category).second) << icon.category;
      current = icon.category;
    }
  }
}

TEST(AvoraSpaceIconsTest, NormalizeKeepsKnownIdentifiers) {
  EXPECT_EQ(NormalizeSpaceIconId("briefcase"), "briefcase");
  EXPECT_EQ(NormalizeSpaceIconId(" briefcase "), "briefcase");
}

TEST(AvoraSpaceIconsTest, NormalizeMapsLegacyEmojiOntoLucide) {
  EXPECT_EQ(NormalizeSpaceIconId("🏠"), "house");
  EXPECT_EQ(NormalizeSpaceIconId("💼"), "briefcase");
  EXPECT_EQ(NormalizeSpaceIconId("✈️"), "plane");
  EXPECT_EQ(NormalizeSpaceIconId("🎮"), "gamepad-2");
}

TEST(AvoraSpaceIconsTest, NormalizeFallsBackForUnknownIcons) {
  EXPECT_EQ(NormalizeSpaceIconId(""), kDefaultSpaceIconId);
  EXPECT_EQ(NormalizeSpaceIconId("🦄"), kDefaultSpaceIconId);
  EXPECT_EQ(NormalizeSpaceIconId("no-such-icon"), kDefaultSpaceIconId);
}

TEST(AvoraSpaceIconsTest, AccentColorsRoundTrip) {
  EXPECT_EQ(ParseSpaceAccentColor("#7C93FF", SK_ColorWHITE),
            SkColorSetRGB(0x7C, 0x93, 0xFF));
  EXPECT_EQ(ParseSpaceAccentColor("#7c93ff", SK_ColorWHITE),
            SkColorSetRGB(0x7C, 0x93, 0xFF));
  EXPECT_EQ(NormalizeSpaceAccentColor("#7c93ff"), "#7C93FF");

  // Unparseable values fall back rather than painting an invisible icon.
  EXPECT_EQ(ParseSpaceAccentColor("blue", SK_ColorWHITE), SK_ColorWHITE);
  EXPECT_EQ(NormalizeSpaceAccentColor("blue"), kDefaultSpaceAccentColor);

  for (std::string_view accent : GetSpaceAccentColors()) {
    EXPECT_EQ(NormalizeSpaceAccentColor(accent), accent);
  }
}

TEST(AvoraSpaceIconsTest, DefaultsRotateSoNewSpacesDiffer) {
  EXPECT_NE(NextDefaultSpaceIconId(0), NextDefaultSpaceIconId(1));
  EXPECT_NE(NextDefaultSpaceAccentColor(0), NextDefaultSpaceAccentColor(1));
  EXPECT_NE(FindSpaceIcon(NextDefaultSpaceIconId(7)), nullptr);
}

TEST(AvoraSpaceTest, RoundTripsIconAndAccentColor) {
  Space space;
  space.id = "space-1";
  space.name = "Work";
  space.icon = "briefcase";
  space.accent_color = "#7C93FF";

  const Space restored = Space::FromDict(space.ToDict());
  EXPECT_EQ(restored.icon, "briefcase");
  EXPECT_EQ(restored.accent_color, "#7C93FF");
  EXPECT_EQ(restored.AccentColor(), SkColorSetRGB(0x7C, 0x93, 0xFF));
}

TEST(AvoraSpaceTest, MigratesSpacesStoredByOlderBuilds) {
  // How a Space looked before Avora moved to Lucide: an emoji icon and a tab
  // group colour id.
  base::DictValue legacy;
  legacy.Set("id", "space-1");
  legacy.Set("name", "Personal");
  legacy.Set("icon", "🏠");
  legacy.Set("color", static_cast<int>(tab_groups::TabGroupColorId::kGreen));

  const Space migrated = Space::FromDict(legacy);
  EXPECT_EQ(migrated.icon, "house");
  EXPECT_EQ(migrated.accent_color,
            LegacySpaceAccentColor(tab_groups::TabGroupColorId::kGreen));
  EXPECT_NE(FindSpaceIcon(migrated.icon), nullptr);
}

TEST(AvoraSpaceTest, FillsInMissingIconAndColor) {
  base::DictValue dict;
  dict.Set("id", "space-1");
  dict.Set("name", "Unnamed");

  const Space space = Space::FromDict(dict);
  EXPECT_EQ(space.icon, kDefaultSpaceIconId);
  EXPECT_EQ(space.accent_color, kDefaultSpaceAccentColor);
}

}  // namespace
}  // namespace avora
