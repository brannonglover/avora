// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_space.h"

#include <optional>

#include "chrome/browser/avora/avora_space_icons.h"
#include "components/tab_groups/tab_group_color.h"

namespace avora {

SkColor Space::AccentColor() const {
  return ParseSpaceAccentColor(
      accent_color,
      ParseSpaceAccentColor(kDefaultSpaceAccentColor, SK_ColorWHITE));
}

base::DictValue Space::ToDict() const {
  return base::DictValue()
      .Set("id", id)
      .Set("name", name)
      .Set("icon", icon)
      .Set("profile_id", profile_id)
      .Set("order", order)
      .Set("color", accent_color)
      .Set("is_active", is_active);
}

Space Space::FromDict(const base::DictValue& dict) {
  Space space;
  if (const std::string* val = dict.FindString("id")) {
    space.id = *val;
  }
  if (const std::string* val = dict.FindString("name")) {
    space.name = *val;
  }
  if (const std::string* val = dict.FindString("icon")) {
    space.icon = NormalizeSpaceIconId(*val);
  } else {
    space.icon = kDefaultSpaceIconId;
  }
  if (const std::string* val = dict.FindString("profile_id")) {
    space.profile_id = *val;
  }
  if (std::optional<int> val = dict.FindInt("order")) {
    space.order = *val;
  }
  if (const std::string* val = dict.FindString("color")) {
    space.accent_color = NormalizeSpaceAccentColor(*val);
  } else if (std::optional<int> val = dict.FindInt("color")) {
    // Written before accent colours were hex; the int was a tab group colour.
    space.accent_color = LegacySpaceAccentColor(
        static_cast<tab_groups::TabGroupColorId>(*val));
  } else {
    space.accent_color = kDefaultSpaceAccentColor;
  }
  if (std::optional<bool> val = dict.FindBool("is_active")) {
    space.is_active = *val;
  }
  return space;
}

}  // namespace avora
