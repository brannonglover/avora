// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_toast_view.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace avora {

namespace {

// ── Colour palette ──────────────────────────────────────────────────────────
// The Avora surface set: the sidebar bubble's background, Avora's accent blue
// for the icon chip, and the shared foreground text colour.
// Opaque and near-black so the card reads as chrome floating above the page
// rather than as part of it, whatever the site behind it looks like.
constexpr SkColor kCardBg = SkColorSetRGB(0x14, 0x16, 0x1C);
constexpr SkColor kAccent = SkColorSetRGB(0x6E, 0xA1, 0xF7);
// An accent rim plus a light inner highlight keeps the edge legible against
// both dark and light pages.
constexpr SkColor kCardBorder = SkColorSetARGB(0x66, 0x6E, 0xA1, 0xF7);
constexpr SkColor kTextColor = SkColorSetRGB(0xF5, 0xF8, 0xFA);
// The glyph sits on a solid accent chip -- the strongest cue that something
// happened.
constexpr SkColor kChipBg = kAccent;
constexpr SkColor kChipIcon = SkColorSetRGB(0x10, 0x14, 0x1A);
// Two shadows: a tight contact shadow and a wide ambient one.
constexpr SkColor kShadowNear = SkColorSetARGB(0x80, 0x00, 0x00, 0x00);
constexpr SkColor kShadowFar = SkColorSetARGB(0x59, 0x00, 0x00, 0x00);

// ── Metrics ─────────────────────────────────────────────────────────────────
constexpr int kCornerRadius = 16;
// Keep (kChipSize - kIconSize) even: ImageView centres with integer division,
// so an odd remainder lands the glyph a pixel off-centre in the chip.
constexpr int kChipSize = 30;
constexpr int kIconSize = 16;
constexpr int kIconTextGap = 11;
constexpr int kHorizontalPadding = 14;
constexpr int kVerticalPadding = 11;
constexpr int kShadowNearOffsetY = 3;
constexpr int kShadowNearBlur = 8;
constexpr int kShadowFarOffsetY = 12;
constexpr int kShadowFarBlur = 32;

// ── Timing ──────────────────────────────────────────────────────────────────
constexpr base::TimeDelta kFadeInDuration = base::Milliseconds(160);
constexpr base::TimeDelta kFadeOutDuration = base::Milliseconds(220);
constexpr base::TimeDelta kVisibleDuration = base::Milliseconds(2400);

// How far above its resting place the card starts, so it settles downwards
// into position instead of appearing flat.
constexpr int kSlideDistance = 12;

// ── Card background ─────────────────────────────────────────────────────────

// Paints the rounded card, its hairline border and its drop shadow inside the
// view's bounds less |shadow_margin| on every side.
class CardBackground : public views::Background {
 public:
  explicit CardBackground(int shadow_margin) : shadow_margin_(shadow_margin) {}

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    gfx::Rect card = view->GetLocalBounds();
    card.Inset(gfx::Insets(shadow_margin_));
    if (card.IsEmpty()) {
      return;
    }

    std::vector<gfx::ShadowValue> shadows;
    shadows.emplace_back(gfx::Vector2d(0, kShadowNearOffsetY), kShadowNearBlur,
                         kShadowNear);
    shadows.emplace_back(gfx::Vector2d(0, kShadowFarOffsetY), kShadowFarBlur,
                         kShadowFar);

    cc::PaintFlags fill_flags;
    fill_flags.setAntiAlias(true);
    fill_flags.setStyle(cc::PaintFlags::kFill_Style);
    fill_flags.setColor(kCardBg);
    fill_flags.setLooper(gfx::CreateShadowDrawLooper(shadows));
    canvas->DrawRoundRect(card, kCornerRadius, fill_flags);

    cc::PaintFlags border_flags;
    border_flags.setAntiAlias(true);
    border_flags.setStyle(cc::PaintFlags::kStroke_Style);
    border_flags.setStrokeWidth(1.5f);
    border_flags.setColor(kCardBorder);
    gfx::RectF border_rect(card);
    border_rect.Inset(0.5f);
    canvas->DrawRoundRect(border_rect, kCornerRadius, border_flags);
  }

 private:
  const int shadow_margin_;
};

// ── Accent chip behind the icon ─────────────────────────────────────────────

class ChipBackground : public views::Background {
 public:
  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(kChipBg);
    const gfx::Rect bounds = view->GetLocalBounds();
    canvas->DrawRoundRect(bounds, bounds.height() / 2.0f, flags);
  }
};

}  // namespace

AvoraToastView::AvoraToastView() {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetOpacity(0.f);
  SetVisible(false);

  // Purely informational: never steal a click from the page underneath.
  SetCanProcessEventsWithinSubtree(false);

  SetBackground(std::make_unique<CardBackground>(kShadowMargin));
  SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(
      kShadowMargin + kVerticalPadding, kShadowMargin + kHorizontalPadding)));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
      kIconTextGap));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto icon = std::make_unique<views::ImageView>();
  icon->SetHorizontalAlignment(views::ImageView::Alignment::kCenter);
  icon->SetVerticalAlignment(views::ImageView::Alignment::kCenter);
  icon->SetBackground(std::make_unique<ChipBackground>());
  icon->SetPreferredSize(gfx::Size(kChipSize, kChipSize));
  icon_ = AddChildView(std::move(icon));

  auto label = std::make_unique<views::Label>(
      std::u16string(),
      views::Label::CustomFont{gfx::FontList({std::string("system-ui")},
                                             gfx::Font::NORMAL, 14,
                                             gfx::Font::Weight::SEMIBOLD)});
  label->SetEnabledColor(kTextColor);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label_ = AddChildView(std::move(label));
}

AvoraToastView::~AvoraToastView() {
  // Stop observing before the layer animator tears down.
  StopObservingImplicitAnimations();
  layer()->GetAnimator()->AbortAllAnimations();
}

void AvoraToastView::Show(const std::u16string& message,
                          const gfx::VectorIcon& icon) {
  icon_->SetImage(ui::ImageModel::FromVectorIcon(icon, kChipIcon, kIconSize));
  label_->SetText(message);
  GetViewAccessibility().SetRole(ax::mojom::Role::kAlert);
  GetViewAccessibility().SetName(message);
  PreferredSizeChanged();

  fading_out_ = false;
  layer()->GetAnimator()->AbortAllAnimations();

  const bool was_visible = GetVisible();
  if (!was_visible) {
    // Start faded out and slightly raised so the card settles into place.
    layer()->SetOpacity(0.f);
    gfx::Transform raised;
    raised.Translate(0, -kSlideDistance);
    layer()->SetTransform(raised);
    SetVisible(true);
  }

  ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
  settings.SetTransitionDuration(kFadeInDuration);
  settings.SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN);
  layer()->SetOpacity(1.f);
  layer()->SetTransform(gfx::Transform());

  // A second press while the toast is up reads as one toast, held longer.
  dismiss_timer_.Start(FROM_HERE, kVisibleDuration,
                       base::BindOnce(&AvoraToastView::StartFadeOut,
                                      weak_factory_.GetWeakPtr()));
}

void AvoraToastView::Hide() {
  dismiss_timer_.Stop();
  if (!GetVisible()) {
    return;
  }
  StartFadeOut();
}

void AvoraToastView::OnImplicitAnimationsCompleted() {
  if (!fading_out_) {
    return;
  }
  fading_out_ = false;
  SetVisible(false);
}

void AvoraToastView::StartFadeOut() {
  dismiss_timer_.Stop();
  fading_out_ = true;

  ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
  settings.SetTransitionDuration(kFadeOutDuration);
  settings.SetTweenType(gfx::Tween::FAST_OUT_LINEAR_IN);
  settings.AddObserver(this);
  layer()->SetOpacity(0.f);
  gfx::Transform raised;
  raised.Translate(0, -kSlideDistance);
  layer()->SetTransform(raised);
}

BEGIN_METADATA(AvoraToastView)
END_METADATA

}  // namespace avora
