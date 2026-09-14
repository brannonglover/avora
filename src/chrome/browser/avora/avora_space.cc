// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_space.h"

namespace avora {

base::DictValue Space::ToDict() const {
  return base::DictValue()
      .Set("id", id)
      .Set("name", name)
      .Set("icon", icon)
      .Set("profile_id", profile_id)
      .Set("order", order)
      .Set("color", static_cast<int>(color))
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
    space.icon = *val;
  }
  if (const std::string* val = dict.FindString("profile_id")) {
    space.profile_id = *val;
  }
  if (std::optional<int> val = dict.FindInt("order")) {
    space.order = *val;
  }
  if (std::optional<int> val = dict.FindInt("color")) {
    space.color = static_cast<tab_groups::TabGroupColorId>(*val);
  }
  if (std::optional<bool> val = dict.FindBool("is_active")) {
    space.is_active = *val;
  }
  return space;
}

}  // namespace avora
