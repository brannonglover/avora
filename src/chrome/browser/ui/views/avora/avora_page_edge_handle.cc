// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_page_edge_handle.h"

#include <string>

#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/views/avora/avora_window_theme.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace avora {

namespace {

constexpr SkColor kAccent = SkColorSetRGB(0x6E, 0xA1, 0xF7);
constexpr int kGripWidth = 4;
constexpr int kGripLength = 40;

}  // namespace

AvoraPageEdgeHandle::AvoraPageEdgeHandle(Edge edge) : edge_(edge) {
  SetVisible(false);
  // Its own layer, so it draws above the contents area beside it.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  GetViewAccessibility().SetRole(ax::mojom::Role::kSplitter);
  GetViewAccessibility().SetName(edge == Edge::kLeft
                                     ? u"Resize page from its left edge"
                                     : u"Resize page from its right edge");
  SetTooltipText(
      u"Drag to resize the page \u00B7 double-click to fill the window");
}

AvoraPageEdgeHandle::~AvoraPageEdgeHandle() = default;

ui::Cursor AvoraPageEdgeHandle::GetCursor(const ui::MouseEvent& event) {
  return ui::Cursor(ui::mojom::CursorType::kEastWestResize);
}

bool AvoraPageEdgeHandle::OnMousePressed(const ui::MouseEvent& event) {
  if (!event.IsOnlyLeftMouseButton() || !delegate_) {
    return false;
  }
  if (event.GetClickCount() == 2) {
    delegate_->OnPageEdgeReset();
    return true;
  }
  // The root view's coordinates stay put while this strip moves with the
  // page's edge under the drag.
  drag_start_ = event.root_location();
  dragging_ = true;
  delegate_->OnPageEdgeDragStarted();
  SchedulePaint();
  return true;
}

bool AvoraPageEdgeHandle::OnMouseDragged(const ui::MouseEvent& event) {
  if (!dragging_ || !delegate_) {
    return false;
  }
  const int dx = event.root_location().x() - drag_start_.x();
  delegate_->OnPageEdgeDragged(edge_ == Edge::kRight ? dx : -dx);
  return true;
}

void AvoraPageEdgeHandle::OnMouseReleased(const ui::MouseEvent& event) {
  EndDrag();
}

void AvoraPageEdgeHandle::OnMouseCaptureLost() {
  EndDrag();
}

void AvoraPageEdgeHandle::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  SchedulePaint();
}

void AvoraPageEdgeHandle::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  SchedulePaint();
}

void AvoraPageEdgeHandle::OnPaint(gfx::Canvas* canvas) {
  // A short grip, centred on the strip: faint at rest so it doesn't nag, in
  // the accent colour while hovered or dragged.
  gfx::Rect grip = GetLocalBounds();
  grip.ClampToCenteredSize(gfx::Size(kGripWidth, kGripLength));
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(dragging_ || hovered_
                     ? kAccent
                     : WindowTheme::PaletteFor(this).text_faint);
  canvas->DrawRoundRect(gfx::RectF(grip), kGripWidth / 2.0f, flags);
}

void AvoraPageEdgeHandle::EndDrag() {
  if (!dragging_) {
    return;
  }
  dragging_ = false;
  if (delegate_) {
    delegate_->OnPageEdgeDragEnded();
  }
  SchedulePaint();
}

BEGIN_METADATA(AvoraPageEdgeHandle)
END_METADATA

}  // namespace avora
