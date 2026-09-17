// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_SPACE_H_
#define CHROME_BROWSER_AVORA_AVORA_SPACE_H_

#include <string>

#include "base/values.h"
#include "third_party/skia/include/core/SkColor.h"

namespace avora {

// A Space is a *workspace*: it owns organizational state only.  Favorites,
// pinned tabs, today tabs, and sidebar ordering all live in SidebarItemStore
// keyed by this Space's id -- not on this struct.
//
// Browser identity (cookies, storage, permissions) comes from the Space's
// BrowserProfile via |profile_id|.  Each Space owns its identity outright:
// SpaceManager mints one per Space and retires it with the Space, so no two
// Spaces share a cookie jar.  The one exception is the first Space on an
// install, which adopts the default identity so existing logins survive.
struct Space {
  std::string id;
  std::string name;

  // Lucide icon identifier, e.g. "briefcase".  Never SVG markup: the
  // identifier is resolved to geometry at paint time via avora_space_icons.h,
  // which keeps stored Spaces independent of how Avora draws icons.
  std::string icon;

  // Owning browser identity.  See avora_profile.h.
  std::string profile_id;

  // Position in the Spaces strip; also drives swipe order.
  int order = 0;

  // Accent colour as "#RRGGBB".  Drives the Space's icon colour and its
  // highlight in the Spaces bar.
  std::string accent_color;

  bool is_active = false;

  // |accent_color| resolved to a paintable colour.
  SkColor AccentColor() const;

  base::DictValue ToDict() const;

  // Tolerates Spaces written by older builds: emoji icons are mapped onto
  // their Lucide equivalent and tab-group accent colours onto hex.
  static Space FromDict(const base::DictValue& dict);
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_SPACE_H_
