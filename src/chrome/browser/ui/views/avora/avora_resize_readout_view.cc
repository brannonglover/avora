// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_resize_readout_view.h"

#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/views/avora/avora_window_theme.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"

namespace avora {

namespace {

// ── Colours and metrics ─────────────────────────────────────────────────────
// The pill floats over whatever the page is showing, so it is opaque, in the
// window's sidebar colour like Avora's toasts, with an accent rim.
constexpr SkColor kAccent = SkColorSetRGB(0x6E, 0xA1, 0xF7);
constexpr SkColor kRim = SkColorSetARGB(0x66, 0x6E, 0xA1, 0xF7);
constexpr SkColor kShadowNear = SkColorSetARGB(0x80, 0x00, 0x00, 0x00);
constexpr SkColor kShadowFar = SkColorSetARGB(0x59, 0x00, 0x00, 0x00);

constexpr int kShadowMargin = 24;
constexpr int kCornerRadius = 14;
constexpr int kHorizontalPadding = 18;
constexpr int kVerticalPadding = 10;
constexpr int kLineGap = 2;

// ── Timing ──────────────────────────────────────────────────────────────────
constexpr base::TimeDelta kFadeInDuration = base::Milliseconds(90);
constexpr base::TimeDelta kLingerDuration = base::Milliseconds(900);
constexpr base::TimeDelta kFadeOutDuration = base::Milliseconds(220);

struct Breakpoint {
  int min_width;
  const char16_t* name;
  const char16_t* range;
};
// Tailwind's min-width breakpoints, which most frameworks match closely.
constexpr auto kBreakpoints = std::to_array<Breakpoint>({
    {1536, u"2xl", u"\u2265 1536"}, {1280, u"xl", u"1280 \u2013 1535"},
    {1024, u"lg", u"1024 \u2013 1279"}, {768, u"md", u"768 \u2013 1023"},
    {640, u"sm", u"640 \u2013 767"},    {0, u"xs", u"< 640"},
});

class PillBackground : public views::Background {
 public:
  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    gfx::Rect pill = view->GetLocalBounds();
    pill.Inset(gfx::Insets(kShadowMargin));
    if (pill.IsEmpty()) {
      return;
    }
    std::vector<gfx::ShadowValue> shadows;
    shadows.emplace_back(gfx::Vector2d(0, 3), 8, kShadowNear);
    shadows.emplace_back(gfx::Vector2d(0, 12), 32, kShadowFar);

    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setStyle(cc::PaintFlags::kFill_Style);
    fill.setColor(WindowTheme::PaletteFor(view).sidebar_top);
    fill.setLooper(gfx::CreateShadowDrawLooper(shadows));
    canvas->DrawRoundRect(pill, kCornerRadius, fill);

    cc::PaintFlags rim;
    rim.setAntiAlias(true);
    rim.setStyle(cc::PaintFlags::kStroke_Style);
    rim.setStrokeWidth(1.5f);
    rim.setColor(kRim);
    gfx::RectF rim_rect(pill);
    rim_rect.Inset(0.75f);
    canvas->DrawRoundRect(rim_rect, kCornerRadius, rim);
  }
};

double ZoomFactorFor(views::WebView* page) {
  content::WebContents* const contents = page ? page->web_contents() : nullptr;
  if (!contents) {
    return 1.0;
  }
  const double factor = blink::ZoomLevelToZoomFactor(
      content::HostZoomMap::GetZoomLevel(contents));
  return factor > 0 ? factor : 1.0;
}

}  // namespace

AvoraResizeReadoutView::AvoraResizeReadoutView() {
  SetVisible(false);
  // Purely informational: never take the mouse from the page below.
  SetCanProcessEventsWithinSubtree(false);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetOpacity(0.f);

  SetBackground(std::make_unique<PillBackground>());
  SetBorder(views::CreateEmptyBorder(
      gfx::Insets::VH(kShadowMargin + kVerticalPadding,
                      kShadowMargin + kHorizontalPadding)));
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), kLineGap));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Menlo so the digits keep their width while they change under the drag.
  size_label_ = AddChildView(std::make_unique<views::Label>(
      std::u16string(),
      views::Label::CustomFont{gfx::FontList({std::string("Menlo")},
                                             gfx::Font::NORMAL, 20,
                                             gfx::Font::Weight::BOLD)}));
  breakpoint_label_ = AddChildView(std::make_unique<views::Label>(
      std::u16string(),
      views::Label::CustomFont{gfx::FontList({std::string("system-ui")},
                                             gfx::Font::NORMAL, 12,
                                             gfx::Font::Weight::MEDIUM)}));

  GetViewAccessibility().SetRole(ax::mojom::Role::kStatus);
}

AvoraResizeReadoutView::~AvoraResizeReadoutView() {
  layer()->GetAnimator()->AbortAllAnimations();
}

void AvoraResizeReadoutView::StartTracking(views::WebView* page) {
  linger_timer_.Stop();
  page_observation_.Reset();
  page_ = page;
  if (!page_) {
    return;
  }
  page_observation_.Observe(page_);

  layer()->GetAnimator()->AbortAllAnimations();
  SetVisible(true);
  Update();
  ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
  settings.SetTransitionDuration(kFadeInDuration);
  layer()->SetOpacity(1.f);
}

void AvoraResizeReadoutView::StopTracking() {
  if (!page_) {
    return;
  }
  Update();
  page_observation_.Reset();
  page_ = nullptr;
  linger_timer_.Start(FROM_HERE, kLingerDuration,
                      base::BindOnce(&AvoraResizeReadoutView::FadeOut,
                                     base::Unretained(this)));
}

std::u16string AvoraResizeReadoutView::GetSizeTextForTesting() const {
  return std::u16string(size_label_->GetText());
}

std::u16string AvoraResizeReadoutView::GetBreakpointTextForTesting() const {
  return std::u16string(breakpoint_label_->GetText());
}

// static
gfx::Size AvoraResizeReadoutView::CssSize(const gfx::Size& dip_size,
                                          double zoom_factor) {
  if (zoom_factor <= 0) {
    zoom_factor = 1.0;
  }
  return gfx::Size(
      static_cast<int>(std::lround(dip_size.width() / zoom_factor)),
      static_cast<int>(std::lround(dip_size.height() / zoom_factor)));
}

// static
std::u16string AvoraResizeReadoutView::BreakpointLabel(int css_width) {
  for (const Breakpoint& breakpoint : kBreakpoints) {
    if (css_width >= breakpoint.min_width) {
      return base::StrCat({breakpoint.name, u"  \u00B7  ", breakpoint.range});
    }
  }
  return std::u16string();
}

void AvoraResizeReadoutView::OnThemeChanged() {
  views::View::OnThemeChanged();
  const SpaceThemePalette& palette = WindowTheme::PaletteFor(this);
  size_label_->SetEnabledColor(palette.text);
  breakpoint_label_->SetEnabledColor(kAccent);
  SchedulePaint();
}

void AvoraResizeReadoutView::OnViewBoundsChanged(views::View* observed_view) {
  Update();
}

void AvoraResizeReadoutView::OnViewIsDeleting(views::View* observed_view) {
  page_observation_.Reset();
  page_ = nullptr;
}

void AvoraResizeReadoutView::Update() {
  if (!page_ || !parent()) {
    return;
  }
  const double zoom = ZoomFactorFor(page_);
  const gfx::Size css = CssSize(page_->size(), zoom);

  std::u16string size_text =
      base::StrCat({base::NumberToString16(css.width()), u" \u00D7 ",
                    base::NumberToString16(css.height())});
  const int zoom_percent = static_cast<int>(std::lround(zoom * 100));
  if (zoom_percent != 100) {
    size_text = base::StrCat(
        {size_text, u"  @", base::NumberToString16(zoom_percent), u"%"});
  }
  size_label_->SetText(size_text);
  breakpoint_label_->SetText(BreakpointLabel(css.width()));
  GetViewAccessibility().SetName(size_text);

  // Centred over the page, wherever it sits in the contents area.
  const gfx::Rect page_bounds = views::View::ConvertRectToTarget(
      page_, parent(), page_->GetLocalBounds());
  gfx::Rect bounds(GetPreferredSize());
  bounds.set_origin(page_bounds.CenterPoint() -
                    gfx::Vector2d(bounds.width() / 2, bounds.height() / 2));
  SetBoundsRect(bounds);
}

void AvoraResizeReadoutView::FadeOut() {
  ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
  settings.SetTransitionDuration(kFadeOutDuration);
  layer()->SetOpacity(0.f);
}

BEGIN_METADATA(AvoraResizeReadoutView)
END_METADATA

}  // namespace avora
