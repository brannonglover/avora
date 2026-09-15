// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LUCIDE_ICON_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LUCIDE_ICON_H_

#include <string_view>

#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/image/image_skia.h"

namespace gfx {
class Canvas;
class Rect;
}  // namespace gfx

namespace avora {

// Renders Lucide icons, Avora's standard icon library.
//
// Lucide ships stroked 24x24 outlines rather than filled glyphs, so instead of
// Chromium's compiled .icon fills these are kept as SVG path data (see
// avora_space_icons.h) and stroked at paint time.  That keeps every icon
// monochrome, uniformly weighted, and tinted by whatever colour the caller
// passes -- which is how a Space's accent colour reaches its icon.

// Lucide's design grid and default stroke weight.  Anything drawn at a
// different size is scaled uniformly, so the stroke stays proportional the way
// Lucide's own SVGs do.
inline constexpr float kLucideCanvasSize = 24.0f;
inline constexpr float kLucideStrokeWidth = 2.0f;

// Parses SVG path data in Lucide's 24x24 space.  Returns an empty path if the
// data cannot be parsed.
SkPath ParseLucidePathData(std::string_view path_data);

// Strokes the icon registered as |icon_id|, centred and scaled to fill
// |bounds|.  Unknown identifiers draw the fallback icon.
void PaintLucideIcon(gfx::Canvas* canvas,
                     const gfx::Rect& bounds,
                     std::string_view icon_id,
                     SkColor color,
                     float stroke_width = kLucideStrokeWidth);

// |icon_id| rendered into a square image of |size| device-independent pixels.
gfx::ImageSkia LucideIconImage(std::string_view icon_id,
                               int size,
                               SkColor color,
                               float stroke_width = kLucideStrokeWidth);

ui::ImageModel LucideIconImageModel(std::string_view icon_id,
                                    int size,
                                    SkColor color,
                                    float stroke_width = kLucideStrokeWidth);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LUCIDE_ICON_H_
