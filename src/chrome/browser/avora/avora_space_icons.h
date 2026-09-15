// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_SPACE_ICONS_H_
#define CHROME_BROWSER_AVORA_AVORA_SPACE_ICONS_H_

#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "chrome/browser/avora/avora_space_icon_data.h"
#include "components/tab_groups/tab_group_color.h"
#include "third_party/skia/include/core/SkColor.h"

namespace avora {

// Avora standardises on Lucide (https://lucide.dev) for Space icons.  A Space
// stores only the Lucide identifier -- never SVG markup -- and the identifier
// is resolved back to geometry here whenever the Space is drawn.

// Fallback for Spaces whose icon is missing or no longer in the catalog.
inline constexpr char kDefaultSpaceIconId[] = "layout-grid";

// Accent colour used when a Space has none recorded.
inline constexpr char kDefaultSpaceAccentColor[] = "#7C93FF";

// The curated set, in picker order.
base::span<const SpaceIcon> GetSpaceIconCatalog();

// Returns the catalog entry for |icon_id|, or nullptr when it is unknown.
const SpaceIcon* FindSpaceIcon(std::string_view icon_id);

// Returns Lucide path data for |icon_id|, falling back to
// kDefaultSpaceIconId so callers never have to handle stale identifiers.
std::string_view GetSpaceIconPathData(std::string_view icon_id);

// Maps whatever a Space has stored onto a usable Lucide identifier.  Handles
// Spaces created before Avora moved to Lucide, whose icon is an emoji, as well
// as identifiers that have since left the catalog.
std::string NormalizeSpaceIconId(std::string_view stored_icon);

// Starter icon for the |index|-th Space, so new Spaces don't all look alike.
std::string NextDefaultSpaceIconId(size_t index);

// The accent colours offered when creating or editing a Space, as "#RRGGBB".
base::span<const std::string_view> GetSpaceAccentColors();

// Starter accent colour for the |index|-th Space.
std::string NextDefaultSpaceAccentColor(size_t index);

// Parses "#RRGGBB" (or "#AARRGGBB").  Returns |fallback| if unparseable.
SkColor ParseSpaceAccentColor(std::string_view hex, SkColor fallback);

// Normalises a stored accent colour to "#RRGGBB", falling back to
// kDefaultSpaceAccentColor.
std::string NormalizeSpaceAccentColor(std::string_view stored_color);

// Accent colour for Spaces created before accent colours were stored as hex,
// which recorded a tab group colour id instead.
std::string LegacySpaceAccentColor(tab_groups::TabGroupColorId color);

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_SPACE_ICONS_H_
