// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_SPACE_H_
#define CHROME_BROWSER_AVORA_AVORA_SPACE_H_

#include <string>

#include "base/values.h"
#include "components/tab_groups/tab_group_color.h"

namespace avora {

// Default icons users can pick from when creating or editing a Space.
inline constexpr const char* kDefaultSpaceIcons[] = {
    "🏠", "💼", "🎮", "📚", "🎵",
    "🧪", "🎨", "✈️",  "🛒", "💬",
    "📷", "🔒", "⭐", "🌙", "🔥",
};
inline constexpr size_t kDefaultSpaceIconCount =
    sizeof(kDefaultSpaceIcons) / sizeof(kDefaultSpaceIcons[0]);

// A Space is a *workspace*: it owns organizational state only.  Favorites,
// pinned tabs, today tabs, and sidebar ordering all live in SidebarItemStore
// keyed by this Space's id -- not on this struct.
//
// Browser identity (cookies, storage, permissions) comes from the Space's
// BrowserProfile via |profile_id|.  Several Spaces may point at the same
// profile_id, sharing login state while keeping separate sidebar contents.
struct Space {
  std::string id;
  std::string name;
  std::string icon;

  // Owning browser identity.  See avora_profile.h.
  std::string profile_id;

  // Position in the Spaces strip; also drives swipe order.
  int order = 0;

  // Retained for the accent colour shown in the Spaces UI.
  tab_groups::TabGroupColorId color = tab_groups::TabGroupColorId::kGrey;

  bool is_active = false;

  base::DictValue ToDict() const;
  static Space FromDict(const base::DictValue& dict);
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_SPACE_H_
