// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_link_preview_tab_indicator.h"

#include "chrome/browser/ui/views/avora/avora_lucide_icon.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace avora {

namespace {

// A window with a smaller window docked in its corner -- Lucide's
// picture-in-picture shape, which is what the preview overlay looks like on
// the tab it belongs to.  Drawn with straight segments rather than Lucide's
// 2-unit corner arcs: at a 16dip tab slot those arcs land below a pixel, and
// the round stroke join already softens the corners.
constexpr char kPreviewIconPath[] = "M21 10V4H3V20H10 M12 12H21V20H12Z";

// Lucide's 2.0 weight goes thin once scaled down to a 16dip tab slot, so the
// marker is stroked slightly heavier to stay legible next to the favicon.
constexpr float kStrokeWidth = 2.4f;

}  // namespace

AvoraLinkPreviewTabIndicator::AvoraLinkPreviewTabIndicator() {
  SetPreferredSize(gfx::Size(kSize, kSize));
  SetVisible(false);
  // Decorative: the tab row underneath keeps handling hover, clicks and drags.
  SetCanProcessEventsWithinSubtree(false);
  GetViewAccessibility().SetRole(ax::mojom::Role::kImage);
  GetViewAccessibility().SetName(u"Link preview open");
}

AvoraLinkPreviewTabIndicator::~AvoraLinkPreviewTabIndicator() = default;

void AvoraLinkPreviewTabIndicator::SetHasPreview(bool has_preview) {
  if (has_preview_ == has_preview) {
    return;
  }
  has_preview_ = has_preview;
  SetVisible(has_preview);
  SchedulePaint();
}

void AvoraLinkPreviewTabIndicator::SetColor(SkColor color) {
  if (color_ == color) {
    return;
  }
  color_ = color;
  SchedulePaint();
}

void AvoraLinkPreviewTabIndicator::OnPaint(gfx::Canvas* canvas) {
  if (!has_preview_) {
    return;
  }
  PaintLucidePathData(canvas, GetLocalBounds(), kPreviewIconPath, color_,
                      kStrokeWidth);
}

BEGIN_METADATA(AvoraLinkPreviewTabIndicator)
END_METADATA

}  // namespace avora
