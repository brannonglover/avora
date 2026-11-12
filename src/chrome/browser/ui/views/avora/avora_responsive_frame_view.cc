// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_responsive_frame_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/supports_user_data.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/views/avora/avora_lucide_icon.h"
#include "chrome/browser/ui/views/avora/avora_window_theme.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_targeter.h"

namespace avora {

namespace {

// ── Per-tab state ───────────────────────────────────────────────────────────

const char kResponsiveStateKey[] = "avora_responsive_state";

struct ResponsiveState : public base::SupportsUserData::Data {
  explicit ResponsiveState(ResponsiveSize size) : size(size) {}
  ResponsiveSize size;
};

// ── Colours and metrics ─────────────────────────────────────────────────────

constexpr SkColor kAccent = SkColorSetRGB(0x6E, 0xA1, 0xF7);
constexpr SkColor kShadowNear = SkColorSetARGB(0x55, 0x00, 0x00, 0x00);
constexpr SkColor kShadowFar = SkColorSetARGB(0x40, 0x00, 0x00, 0x00);

constexpr int kPageCornerRadius = 6;
constexpr int kGripLength = 36;
constexpr int kGripThickness = 4;
constexpr int kButtonHeight = 26;
constexpr int kButtonPadding = 10;
constexpr int kToolbarGap = 6;
constexpr int kReadoutGap = 8;
constexpr int kGroupGap = 16;

// The devices the preset chips stand for, in CSS pixels.  0x0 is "Fill".
struct Preset {
  const char16_t* label;
  const char16_t* tooltip;
  int width;
  int height;
};
constexpr auto kPresets = std::to_array<Preset>({
    {u"SE", u"iPhone SE · 375 × 667", 375, 667},
    {u"iPhone", u"iPhone · 390 × 844", 390, 844},
    {u"Pro Max", u"iPhone Pro Max · 430 × 932", 430, 932},
    {u"iPad mini", u"iPad mini · 768 × 1024", 768, 1024},
    {u"iPad", u"iPad · 820 × 1180", 820, 1180},
    {u"iPad Pro", u"iPad Pro · 1024 × 1366", 1024, 1366},
    {u"Fill", u"Fill the window", 0, 0},
});

// Lucide "rotate-cw" and "x".
constexpr char kRotatePath[] =
    "M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8"
    "M21 3v5h-5";
constexpr char kClosePath[] =
    "M18 6 6 18"
    "M6 6l12 12";

gfx::FontList ReadoutFont() {
  return gfx::FontList({std::string("Menlo")}, gfx::Font::NORMAL, 12,
                       gfx::Font::Weight::SEMIBOLD);
}

gfx::FontList ChipFont() {
  return gfx::FontList({std::string("system-ui")}, gfx::Font::NORMAL, 12,
                       gfx::Font::Weight::MEDIUM);
}

int CssToDip(int css, double zoom) {
  return static_cast<int>(std::lround(css * zoom));
}

int DipToCss(int dip, double zoom) {
  return static_cast<int>(std::lround(dip / zoom));
}

}  // namespace

// ── Per-tab state ───────────────────────────────────────────────────────────

std::optional<ResponsiveSize> GetResponsiveSize(
    content::WebContents* contents) {
  if (!contents) {
    return std::nullopt;
  }
  auto* state =
      static_cast<ResponsiveState*>(contents->GetUserData(kResponsiveStateKey));
  if (!state) {
    return std::nullopt;
  }
  return state->size;
}

void SetResponsiveSize(content::WebContents* contents,
                       std::optional<ResponsiveSize> size) {
  if (!contents) {
    return;
  }
  if (!size) {
    contents->RemoveUserData(kResponsiveStateKey);
    return;
  }
  contents->SetUserData(kResponsiveStateKey,
                        std::make_unique<ResponsiveState>(*size));
}

// ── Geometry ────────────────────────────────────────────────────────────────

ResponsiveLayout ComputeResponsiveLayout(const gfx::Rect& available,
                                         const ResponsiveSize& requested) {
  ResponsiveLayout layout;
  layout.toolbar = gfx::Rect(available.x(), available.y(), available.width(),
                             std::min(kResponsiveToolbarHeight,
                                      available.height()));

  layout.max_page = gfx::Size(
      std::max(0, available.width() - 2 * kResponsiveSideGutter),
      std::max(0, available.height() - kResponsiveToolbarHeight -
                      kResponsiveBottomGutter));

  // Each axis: fill when unset, else the request held between the minimum and
  // what fits.  A window too small for the minimum gets what fits.
  auto resolve = [](int requested_size, int max_size) {
    if (requested_size <= 0) {
      return max_size;
    }
    return std::min(max_size,
                    std::max(requested_size, kResponsiveMinPageSize));
  };
  const int width = resolve(requested.width, layout.max_page.width());
  const int height = resolve(requested.height, layout.max_page.height());

  layout.page =
      gfx::Rect(available.x() + (available.width() - width) / 2,
                available.y() + kResponsiveToolbarHeight, width, height);

  const int offset = kResponsiveHandleGap + kResponsiveHandleThickness;
  layout.left_handle =
      gfx::Rect(layout.page.x() - offset, layout.page.y(),
                kResponsiveHandleThickness, layout.page.height());
  layout.right_handle =
      gfx::Rect(layout.page.right() + kResponsiveHandleGap, layout.page.y(),
                kResponsiveHandleThickness, layout.page.height());
  layout.bottom_handle =
      gfx::Rect(layout.page.x(), layout.page.bottom() + kResponsiveHandleGap,
                layout.page.width(), kResponsiveHandleThickness);
  return layout;
}

int SnapResponsiveWidth(int css_width) {
  for (int snap : kResponsiveSnapWidths) {
    if (std::abs(css_width - snap) <= kResponsiveSnapDistance) {
      return snap;
    }
  }
  return css_width;
}

const char* ResponsiveBreakpointName(int css_width) {
  if (css_width >= 1536) {
    return "2xl";
  }
  if (css_width >= 1280) {
    return "xl";
  }
  if (css_width >= 1024) {
    return "lg";
  }
  if (css_width >= 768) {
    return "md";
  }
  if (css_width >= 640) {
    return "sm";
  }
  return "xs";
}

namespace {

// ── Toolbar button ──────────────────────────────────────────────────────────

// A pill with either a short label (the device presets) or a Lucide glyph
// (rotate, close), in the window's palette.
class ToolbarButton : public views::Button {
  METADATA_HEADER(ToolbarButton, views::Button)

 public:
  ToolbarButton(PressedCallback callback,
                std::u16string text,
                const char* icon_path,
                std::u16string tooltip)
      : views::Button(std::move(callback)),
        text_(std::move(text)),
        icon_path_(icon_path) {
    SetTooltipText(tooltip);
    GetViewAccessibility().SetName(tooltip);
    SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
  }

  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    SchedulePaint();
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    if (icon_path_) {
      return gfx::Size(kButtonHeight, kButtonHeight);
    }
    return gfx::Size(
        gfx::Canvas::GetStringWidth(text_, ChipFont()) + 2 * kButtonPadding,
        kButtonHeight);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const SpaceThemePalette& palette = WindowTheme::PaletteFor(this);
    const bool hovered = GetState() == STATE_HOVERED ||
                         GetState() == STATE_PRESSED;
    const gfx::RectF bounds(GetLocalBounds());

    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setStyle(cc::PaintFlags::kFill_Style);
    if (selected_) {
      fill.setColor(SkColorSetA(kAccent, 0x40));
      canvas->DrawRoundRect(bounds, bounds.height() / 2, fill);
    } else if (hovered) {
      fill.setColor(palette.tab_hover);
      canvas->DrawRoundRect(bounds, bounds.height() / 2, fill);
    }

    const SkColor color =
        selected_ ? color_utils::AlphaBlend(palette.text, kAccent, 0.35f)
        : hovered ? palette.text
                  : palette.text_dim;
    if (icon_path_) {
      gfx::Rect glyph = GetLocalBounds();
      glyph.ClampToCenteredSize(gfx::Size(14, 14));
      PaintLucidePathData(canvas, glyph, icon_path_, color);
      return;
    }
    canvas->DrawStringRectWithFlags(text_, ChipFont(), color, GetLocalBounds(),
                                    gfx::Canvas::TEXT_ALIGN_CENTER);
  }

 private:
  const std::u16string text_;
  const char* const icon_path_;
  bool selected_ = false;
};

BEGIN_METADATA(ToolbarButton)
END_METADATA

// ── Drag handle ─────────────────────────────────────────────────────────────

// One edge of the page.  A side handle moves both sides at once, so the page
// stays centred and the width changes by twice the drag; the bottom handle
// moves the bottom edge alone.  Double-click fills that axis again.
class Handle : public views::View {
  METADATA_HEADER(Handle, views::View)

 public:
  enum class Edge { kLeft, kRight, kBottom };

  Handle(AvoraResponsiveFrameView* frame, Edge edge)
      : frame_(frame), edge_(edge) {
    GetViewAccessibility().SetRole(ax::mojom::Role::kSplitter);
    GetViewAccessibility().SetName(edge == Edge::kBottom
                                       ? u"Resize page height"
                                       : u"Resize page width");
    SetTooltipText(edge == Edge::kBottom
                       ? u"Drag to resize · double-click to fill"
                       : u"Drag to resize · ⌥ to skip snapping "
                         u"· double-click to fill");
  }

  bool horizontal() const { return edge_ != Edge::kBottom; }

  // views::View:
  ui::Cursor GetCursor(const ui::MouseEvent& event) override {
    return ui::Cursor(horizontal() ? ui::mojom::CursorType::kEastWestResize
                                   : ui::mojom::CursorType::kNorthSouthResize);
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!event.IsOnlyLeftMouseButton()) {
      return false;
    }
    if (event.GetClickCount() == 2) {
      ResponsiveSize size = frame_->requested_size();
      (horizontal() ? size.width : size.height) = 0;
      frame_->SetRequestedSize(size);
      return true;
    }
    // Start from the size on screen, which differs from the request when the
    // request is "fill" or did not fit.
    start_page_ = frame_->current_layout().page.size();
    start_point_ = event.root_location();
    dragging_ = true;
    SchedulePaint();
    return true;
  }

  bool OnMouseDragged(const ui::MouseEvent& event) override {
    if (!dragging_) {
      return false;
    }
    const gfx::Vector2d delta = event.root_location() - start_point_;
    const gfx::Size max_page = frame_->current_layout().max_page;
    ResponsiveSize size = frame_->requested_size();
    // Both axes become explicit once dragged, so the page holds still while
    // the other one is being worked on.
    size.width = start_page_.width();
    size.height = start_page_.height();

    if (horizontal()) {
      const int sign = edge_ == Edge::kLeft ? -1 : 1;
      int width = start_page_.width() + 2 * sign * delta.x();
      if (!event.IsAltDown()) {
        const double zoom = frame_->ZoomFactor();
        width = CssToDip(SnapResponsiveWidth(DipToCss(width, zoom)), zoom);
      }
      size.width = std::clamp(width, kResponsiveMinPageSize,
                              std::max(kResponsiveMinPageSize,
                                       max_page.width()));
    } else {
      size.height = std::clamp(start_page_.height() + delta.y(),
                               kResponsiveMinPageSize,
                               std::max(kResponsiveMinPageSize,
                                        max_page.height()));
    }
    frame_->SetRequestedSize(size, /*dragging=*/true);
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override { EndDrag(); }
  void OnMouseCaptureLost() override { EndDrag(); }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    hovered_ = true;
    SchedulePaint();
  }
  void OnMouseExited(const ui::MouseEvent& event) override {
    hovered_ = false;
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const SpaceThemePalette& palette = WindowTheme::PaletteFor(this);
    gfx::Rect grip = GetLocalBounds();
    grip.ClampToCenteredSize(horizontal()
                                 ? gfx::Size(kGripThickness, kGripLength)
                                 : gfx::Size(kGripLength, kGripThickness));
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(dragging_ || hovered_ ? kAccent : palette.text_faint);
    canvas->DrawRoundRect(gfx::RectF(grip), kGripThickness / 2.0f, flags);
  }

 private:
  void EndDrag() {
    if (!dragging_) {
      return;
    }
    dragging_ = false;
    frame_->SetRequestedSize(frame_->requested_size(), /*dragging=*/false);
    SchedulePaint();
  }

  const raw_ptr<AvoraResponsiveFrameView> frame_;
  const Edge edge_;
  bool dragging_ = false;
  bool hovered_ = false;
  gfx::Size start_page_;
  gfx::Point start_point_;
};

BEGIN_METADATA(Handle)
END_METADATA

}  // namespace

// ── The view ────────────────────────────────────────────────────────────────

AvoraResponsiveFrameView::AvoraResponsiveFrameView(Delegate* delegate)
    : delegate_(delegate) {
  SetVisible(false);
  SetEventTargeter(std::make_unique<views::ViewTargeter>(this));
  // Its own layer, so it composites above the page container's layered
  // children in view order; the page shows through the hole it leaves.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);

  // The toolbar: the size readout, the preset chips, rotate and close.
  auto toolbar = std::make_unique<views::View>();
  auto* toolbar_layout =
      toolbar->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          kToolbarGap));
  toolbar_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  toolbar_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto readout = std::make_unique<views::View>();
  readout->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kReadoutGap));
  size_label_ = readout->AddChildView(
      std::make_unique<views::Label>(std::u16string(),
                                     views::Label::CustomFont{ReadoutFont()}));
  size_label_->GetViewAccessibility().SetRole(ax::mojom::Role::kStatus);
  breakpoint_label_ = readout->AddChildView(
      std::make_unique<views::Label>(std::u16string(),
                                     views::Label::CustomFont{ReadoutFont()}));
  breakpoint_label_->SetTooltipText(
      u"Breakpoint: sm 640 · md 768 · lg 1024 · xl 1280 "
      u"· 2xl 1536");
  readout->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 0, 0,
                                                              kGroupGap));
  toolbar->AddChildView(std::move(readout));

  auto presets = std::make_unique<views::View>();
  presets->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 2));
  for (const Preset& preset : kPresets) {
    presets->AddChildView(std::make_unique<ToolbarButton>(
        base::BindRepeating(&AvoraResponsiveFrameView::ApplyPreset,
                            base::Unretained(this), preset.width,
                            preset.height),
        preset.label, nullptr, preset.tooltip));
  }
  presets->SetProperty(views::kMarginsKey,
                       gfx::Insets::TLBR(0, 0, 0, kGroupGap - kToolbarGap));
  presets_ = toolbar->AddChildView(std::move(presets));

  toolbar->AddChildView(std::make_unique<ToolbarButton>(
      base::BindRepeating(&AvoraResponsiveFrameView::Rotate,
                          base::Unretained(this)),
      std::u16string(), kRotatePath, u"Rotate"));
  toolbar->AddChildView(std::make_unique<ToolbarButton>(
      base::BindRepeating(&AvoraResponsiveFrameView::Exit,
                          base::Unretained(this)),
      std::u16string(), kClosePath, u"Exit responsive mode (⌥⌘R)"));
  toolbar_ = AddChildView(std::move(toolbar));

  left_handle_ =
      AddChildView(std::make_unique<Handle>(this, Handle::Edge::kLeft));
  right_handle_ =
      AddChildView(std::make_unique<Handle>(this, Handle::Edge::kRight));
  bottom_handle_ =
      AddChildView(std::make_unique<Handle>(this, Handle::Edge::kBottom));
}

AvoraResponsiveFrameView::~AvoraResponsiveFrameView() = default;

void AvoraResponsiveFrameView::Refresh() {
  content::WebContents* const contents = delegate_->GetResponsiveWebContents();
  requested_ = GetResponsiveSize(contents).value_or(ResponsiveSize());
  ObserveZoomFor(contents);
  InvalidateLayout();
  UpdateReadout();
}

void AvoraResponsiveFrameView::SetRequestedSize(const ResponsiveSize& size,
                                                bool dragging) {
  if (dragging_ != dragging) {
    // The drag-only outline of the largest size comes or goes.
    dragging_ = dragging;
    SchedulePaint();
  }
  content::WebContents* const contents = delegate_->GetResponsiveWebContents();
  if (!contents) {
    return;
  }
  const bool changed = size != requested_;
  requested_ = size;
  SetResponsiveSize(contents, size);
  if (changed) {
    InvalidateLayout();
    delegate_->OnResponsiveSizeChanged();
  }
  UpdateReadout();
}

void AvoraResponsiveFrameView::Exit() {
  SetResponsiveSize(delegate_->GetResponsiveWebContents(), std::nullopt);
  delegate_->OnResponsiveSizeChanged();
}

double AvoraResponsiveFrameView::ZoomFactor() const {
  content::WebContents* const contents = delegate_->GetResponsiveWebContents();
  if (!contents) {
    return 1.0;
  }
  const double factor = blink::ZoomLevelToZoomFactor(
      content::HostZoomMap::GetZoomLevel(contents));
  return factor > 0 ? factor : 1.0;
}

void AvoraResponsiveFrameView::Layout(PassKey) {
  const gfx::Rect old_page = layout_.page;
  layout_ = ComputeResponsiveLayout(GetLocalBounds(), requested_);
  // The backdrop's hole and shadow follow the page; this view's own bounds
  // may not have changed, so nothing else would repaint them.
  if (layout_.page != old_page) {
    SchedulePaint();
  }

  // The presets go first when the window is too narrow for the whole row.
  // BoxLayout drops a hidden child's width, spacing and margin, so add those
  // back to judge whether the full row would fit.  Visibility only changes
  // when it must: each change invalidates layout, and toggling it here every
  // pass would never settle.
  int full_width = toolbar_->GetPreferredSize().width();
  if (!presets_->GetVisible()) {
    full_width += presets_->GetPreferredSize().width() + kGroupGap;
  }
  const bool presets_fit = full_width <= layout_.toolbar.width();
  if (presets_->GetVisible() != presets_fit) {
    presets_->SetVisible(presets_fit);
  }
  toolbar_->SetBoundsRect(layout_.toolbar);
  left_handle_->SetBoundsRect(layout_.left_handle);
  right_handle_->SetBoundsRect(layout_.right_handle);
  bottom_handle_->SetBoundsRect(layout_.bottom_handle);
  UpdateReadout();
}

bool AvoraResponsiveFrameView::DoesIntersectRect(const views::View* target,
                                                 const gfx::Rect& rect) const {
  // Only the toolbar row and the handles; the page and the bare backdrop let
  // events through to the page container below.
  return layout_.toolbar.Intersects(rect) ||
         layout_.left_handle.Intersects(rect) ||
         layout_.right_handle.Intersects(rect) ||
         layout_.bottom_handle.Intersects(rect);
}

void AvoraResponsiveFrameView::OnPaint(gfx::Canvas* canvas) {
  const SpaceThemePalette& palette = WindowTheme::PaletteFor(this);
  if (layout_.page.IsEmpty()) {
    canvas->DrawColor(palette.sidebar_top);
    return;
  }

  // Everything but the page: the page container is below this view, and the
  // page must show through.
  const SkRRect page_rrect =
      SkRRect::MakeRectXY(gfx::RectToSkRect(layout_.page), kPageCornerRadius,
                          kPageCornerRadius);
  canvas->sk_canvas()->clipRRect(page_rrect, SkClipOp::kDifference,
                                 /*do_anti_alias=*/true);
  canvas->DrawColor(palette.sidebar_top);

  // A soft shadow lifts the page off the backdrop like a device on a desk.
  std::vector<gfx::ShadowValue> shadows;
  shadows.emplace_back(gfx::Vector2d(0, 2), 6, kShadowNear);
  shadows.emplace_back(gfx::Vector2d(0, 10), 30, kShadowFar);
  cc::PaintFlags shadow_flags;
  shadow_flags.setAntiAlias(true);
  shadow_flags.setStyle(cc::PaintFlags::kFill_Style);
  shadow_flags.setColor(palette.sidebar_top);
  shadow_flags.setLooper(gfx::CreateShadowDrawLooper(shadows));
  canvas->DrawRoundRect(gfx::RectF(layout_.page), kPageCornerRadius,
                        shadow_flags);

  // While dragging, a hairline outline shows the full extent the page could
  // grow to.
  if (dragging_) {
    gfx::Rect max_rect(
        (width() - layout_.max_page.width()) / 2, layout_.page.y(),
        layout_.max_page.width(), layout_.max_page.height());
    cc::PaintFlags outline;
    outline.setAntiAlias(true);
    outline.setStyle(cc::PaintFlags::kStroke_Style);
    outline.setStrokeWidth(1);
    outline.setColor(palette.hairline);
    gfx::RectF outline_rect(max_rect);
    outline_rect.Inset(0.5f);
    canvas->DrawRoundRect(outline_rect, kPageCornerRadius, outline);
  }
}

void AvoraResponsiveFrameView::OnThemeChanged() {
  views::View::OnThemeChanged();
  const SpaceThemePalette& palette = WindowTheme::PaletteFor(this);
  size_label_->SetEnabledColor(palette.text);
  breakpoint_label_->SetEnabledColor(palette.text_dim);
  SchedulePaint();
}

void AvoraResponsiveFrameView::UpdateReadout() {
  const double zoom = ZoomFactor();
  const int css_width = DipToCss(layout_.page.width(), zoom);
  const int css_height = DipToCss(layout_.page.height(), zoom);

  std::u16string size_text =
      base::StrCat({base::NumberToString16(css_width), u" × ",
                    base::NumberToString16(css_height)});
  const int zoom_percent = static_cast<int>(std::lround(zoom * 100));
  if (zoom_percent != 100) {
    size_text = base::StrCat(
        {size_text, u"  @", base::NumberToString16(zoom_percent), u"%"});
  }
  size_label_->SetText(size_text);
  breakpoint_label_->SetText(
      base::ASCIIToUTF16(ResponsiveBreakpointName(css_width)));

  // Light up the preset the page is at.  "Fill" is lit only when both axes
  // fill; a device preset when both requested axes match it.
  const auto& chips = presets_->children();
  for (size_t i = 0; i < chips.size() && i < kPresets.size(); ++i) {
    const Preset& preset = kPresets.at(i);
    bool selected;
    if (preset.width == 0) {
      selected = requested_.width == 0 && requested_.height == 0;
    } else {
      selected = requested_.width == CssToDip(preset.width, zoom) &&
                 requested_.height == CssToDip(preset.height, zoom);
    }
    static_cast<ToolbarButton*>(chips[i].get())->SetSelected(selected);
  }
}

void AvoraResponsiveFrameView::ObserveZoomFor(content::WebContents* contents) {
  content::HostZoomMap* const zoom_map =
      contents ? content::HostZoomMap::GetForWebContents(contents) : nullptr;
  if (zoom_map == observed_zoom_map_) {
    return;
  }
  observed_zoom_map_ = zoom_map;
  zoom_subscription_ = {};
  if (zoom_map) {
    // Any zoom change re-reads the readout; working out whether it was this
    // tab's host costs more than the relabel does.
    zoom_subscription_ = zoom_map->AddZoomLevelChangedCallback(
        base::BindRepeating(
            [](AvoraResponsiveFrameView* view,
               const content::HostZoomMap::ZoomLevelChange&) {
              view->UpdateReadout();
            },
            base::Unretained(this)));
  }
}

void AvoraResponsiveFrameView::ApplyPreset(int css_width, int css_height) {
  const double zoom = ZoomFactor();
  SetRequestedSize(
      ResponsiveSize{css_width ? CssToDip(css_width, zoom) : 0,
                     css_height ? CssToDip(css_height, zoom) : 0});
}

void AvoraResponsiveFrameView::Rotate() {
  const gfx::Size page = layout_.page.size();
  SetRequestedSize(ResponsiveSize{page.height(), page.width()});
}

BEGIN_METADATA(AvoraResponsiveFrameView)
END_METADATA

}  // namespace avora
