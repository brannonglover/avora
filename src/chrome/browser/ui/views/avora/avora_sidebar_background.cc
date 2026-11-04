// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_sidebar_background.h"

#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "chrome/browser/avora/avora_space_theme.h"
#include "chrome/browser/ui/views/avora/avora_window_theme.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/background.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace avora {

namespace {

class SidebarBackground : public views::Background {
 public:
  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    if (!PaintSidebarGradient(canvas, view)) {
      canvas->DrawColor(WindowTheme::PaletteFor(view).sidebar_top);
    }
  }
};

}  // namespace

bool PaintSidebarGradient(gfx::Canvas* canvas, const views::View* view) {
  const SpaceThemePalette& palette = WindowTheme::PaletteFor(view);
  if (palette.sidebar_top == palette.sidebar_bottom) {
    return false;
  }
  const gfx::Rect bounds = view->GetLocalBounds();
  // Where this view sits in the window, as fractions of its height.
  float top = 0.0f;
  float bottom = 1.0f;
  if (const views::Widget* widget = view->GetWidget();
      widget && widget->GetRootView()->height() > 0) {
    gfx::Point origin;
    views::View::ConvertPointToWidget(view, &origin);
    const float height = widget->GetRootView()->height();
    top = origin.y() / height;
    bottom = (origin.y() + bounds.height()) / height;
  }
  const SkPoint points[] = {SkPoint::Make(0, bounds.y()),
                            SkPoint::Make(0, bounds.bottom())};
  const SkColor4f colors[] = {SkColor4f::FromColor(palette.SidebarAt(top)),
                              SkColor4f::FromColor(palette.SidebarAt(bottom))};
  cc::PaintFlags flags;
  flags.setShader(cc::PaintShader::MakeLinearGradient(points, colors, nullptr,
                                                      2, SkTileMode::kClamp));
  canvas->DrawRect(bounds, flags);
  return true;
}

std::unique_ptr<views::Background> CreateSidebarBackground() {
  return std::make_unique<SidebarBackground>();
}

}  // namespace avora
