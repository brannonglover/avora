// Avora browser layout constants and bounds helpers.

#ifndef AVORA_LAYOUT_H_
#define AVORA_LAYOUT_H_

#include <algorithm>

#include "include/internal/cef_types_wrappers.h"

namespace avora {

inline constexpr int kToolbarHeight = 54;
inline constexpr int kSidebarWidthDefault = 236;
inline constexpr int kSidebarMinWidth = 184;
inline constexpr int kSidebarMaxWidth = 360;
inline constexpr int kContentInset = 0;
inline constexpr int kMinContentWidth = 320;
inline constexpr int kMinContentHeight = 240;

inline int ClampSidebarWidth(int width, int window_width) {
  const int max_width = std::max(
      kSidebarMinWidth,
      std::min(kSidebarMaxWidth,
               window_width - kMinContentWidth - kContentInset * 2));
  if (width < kSidebarMinWidth) {
    return kSidebarMinWidth;
  }
  if (width > max_width) {
    return max_width;
  }
  return width;
}

inline CefRect ContentBounds(int window_width,
                             int window_height,
                             int sidebar_width) {
  const int x = sidebar_width + kContentInset;
  const int y = kToolbarHeight + kContentInset;
  const int width = std::max(
      kMinContentWidth, window_width - sidebar_width - kContentInset * 2);
  const int height = std::max(
      kMinContentHeight, window_height - kToolbarHeight - kContentInset * 2);
  return CefRect(x, y, width, height);
}

}  // namespace avora

#endif  // AVORA_LAYOUT_H_
