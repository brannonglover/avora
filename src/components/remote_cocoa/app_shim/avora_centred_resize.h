// Copyright 2026 Avora. All rights reserved.

#ifndef COMPONENTS_REMOTE_COCOA_APP_SHIM_AVORA_CENTRED_RESIZE_H_
#define COMPONENTS_REMOTE_COCOA_APP_SHIM_AVORA_CENTRED_RESIZE_H_

#include <algorithm>
#include <cmath>

namespace remote_cocoa {

// Avora resizes browser windows around their centre: dragging one edge moves
// the opposite edge the same distance the other way, so the window shrinks or
// grows in place.  It is what AppKit does when Option is held, made the
// default, so a window can be pulled down to a phone's width without walking
// off to one side.

// One axis of a centred resize.  The dragged edge is at |dragged_edge| (it
// follows the mouse); the result is the span centred on |centre| that reaches
// it, held between |min_length| and |max_length| (0 for no maximum).
struct CentredSpan {
  double origin = 0;
  double length = 0;
};

inline CentredSpan CentredResizeSpan(double dragged_edge,
                                     double centre,
                                     double min_length,
                                     double max_length) {
  double length = 2 * std::abs(dragged_edge - centre);
  length = std::max(length, min_length);
  if (max_length > 0) {
    length = std::min(length, max_length);
  }
  // Whole points keep the window's edges crisp; the centre may be half a
  // point off, which nobody can see.
  return {std::round(centre - length / 2), std::round(length)};
}

}  // namespace remote_cocoa

#endif  // COMPONENTS_REMOTE_COCOA_APP_SHIM_AVORA_CENTRED_RESIZE_H_
