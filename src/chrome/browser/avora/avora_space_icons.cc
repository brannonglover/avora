// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_space_icons.h"

#include <cstdint>
#include <iterator>
#include <optional>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"

namespace avora {

namespace {

// Rotated through when a Space is created without an explicit icon, so a user
// who makes several Spaces in a row can still tell them apart.
constexpr std::string_view kStarterIcons[] = {
    "briefcase", "house",         "code",          "palette",   "music",
    "book",      "shopping-cart", "plane",         "gamepad-2", "camera",
    "heart",     "graduation-cap"};

// Picker palette.  Tuned for the dark sidebar: saturated enough to read at
// 18px, light enough not to vibrate against the bar background.
constexpr std::string_view kAccentColors[] = {
    "#7C93FF",  // Indigo
    "#5AC8FA",  // Sky
    "#34D3C1",  // Teal
    "#4FD1A5",  // Green
    "#F5C451",  // Amber
    "#FF8A5B",  // Orange
    "#FF6B81",  // Coral
    "#F78FD0",  // Pink
    "#D98CFF",  // Violet
    "#9AA7B8",  // Slate
};

// Spaces created before Avora adopted Lucide stored an emoji.  Every emoji the
// old picker offered maps onto the closest icon in the curated set.
struct LegacyIconMapping {
  std::string_view emoji;
  std::string_view icon_id;
};

constexpr LegacyIconMapping kLegacyEmojiIcons[] = {
    {"🏠", "house"},         {"💼", "briefcase"},
    {"🎮", "gamepad-2"},     {"📚", "book"},
    {"🎵", "music"},         {"🧪", "microscope"},
    {"🎨", "palette"},       {"✈️", "plane"},
    {"✈", "plane"},          {"🛒", "shopping-cart"},
    {"💬", "message-circle"}, {"📷", "camera"},
    {"🔒", "lock"},          {"⭐", "star"},
    {"🌙", "moon"},          {"🔥", "flame"},
};

std::optional<SkColor> ParseHexColor(std::string_view hex) {
  std::string_view digits = base::TrimWhitespaceASCII(hex, base::TRIM_ALL);
  if (digits.empty() || digits.front() != '#') {
    return std::nullopt;
  }
  digits.remove_prefix(1);
  if (digits.size() != 6 && digits.size() != 8) {
    return std::nullopt;
  }
  uint32_t value = 0;
  if (!base::HexStringToUInt(digits, &value)) {
    return std::nullopt;
  }
  return digits.size() == 6 ? SkColorSetA(value, SK_AlphaOPAQUE)
                            : static_cast<SkColor>(value);
}

}  // namespace

const SpaceIcon* FindSpaceIcon(std::string_view icon_id) {
  for (const SpaceIcon& icon : GetSpaceIconCatalog()) {
    if (icon.id == icon_id) {
      return &icon;
    }
  }
  return nullptr;
}

std::string_view GetSpaceIconPathData(std::string_view icon_id) {
  if (const SpaceIcon* icon = FindSpaceIcon(icon_id)) {
    return icon->path_data;
  }
  if (const SpaceIcon* fallback = FindSpaceIcon(kDefaultSpaceIconId)) {
    return fallback->path_data;
  }
  return std::string_view();
}

std::string NormalizeSpaceIconId(std::string_view stored_icon) {
  const std::string_view trimmed =
      base::TrimWhitespaceASCII(stored_icon, base::TRIM_ALL);
  if (FindSpaceIcon(trimmed)) {
    return std::string(trimmed);
  }
  for (const LegacyIconMapping& mapping : kLegacyEmojiIcons) {
    if (mapping.emoji == trimmed) {
      return std::string(mapping.icon_id);
    }
  }
  return std::string(kDefaultSpaceIconId);
}

std::string NextDefaultSpaceIconId(size_t index) {
  return std::string(kStarterIcons[index % std::size(kStarterIcons)]);
}

base::span<const std::string_view> GetSpaceAccentColors() {
  return base::span<const std::string_view>(kAccentColors);
}

std::string NextDefaultSpaceAccentColor(size_t index) {
  return std::string(kAccentColors[index % std::size(kAccentColors)]);
}

SkColor ParseSpaceAccentColor(std::string_view hex, SkColor fallback) {
  return ParseHexColor(hex).value_or(fallback);
}

std::string NormalizeSpaceAccentColor(std::string_view stored_color) {
  const std::optional<SkColor> parsed = ParseHexColor(stored_color);
  if (!parsed.has_value()) {
    return std::string(kDefaultSpaceAccentColor);
  }
  return base::StringPrintf("#%02X%02X%02X", SkColorGetR(*parsed),
                            SkColorGetG(*parsed), SkColorGetB(*parsed));
}

std::string LegacySpaceAccentColor(tab_groups::TabGroupColorId color) {
  switch (color) {
    case tab_groups::TabGroupColorId::kGrey:
      return "#9AA7B8";
    case tab_groups::TabGroupColorId::kBlue:
      return "#7C93FF";
    case tab_groups::TabGroupColorId::kRed:
      return "#FF6B81";
    case tab_groups::TabGroupColorId::kYellow:
      return "#F5C451";
    case tab_groups::TabGroupColorId::kGreen:
      return "#4FD1A5";
    case tab_groups::TabGroupColorId::kPink:
      return "#F78FD0";
    case tab_groups::TabGroupColorId::kPurple:
      return "#D98CFF";
    case tab_groups::TabGroupColorId::kCyan:
      return "#5AC8FA";
    case tab_groups::TabGroupColorId::kOrange:
      return "#FF8A5B";
    default:
      return std::string(kDefaultSpaceAccentColor);
  }
}

}  // namespace avora
