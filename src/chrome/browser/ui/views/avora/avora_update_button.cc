// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_update_button.h"

#include <cmath>
#include <numbers>
#include <string>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/views/avora/avora_lucide_icon.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace avora {

namespace {

// Matched to the Space bar's own metrics, like the downloads button.
// The glyph is larger than the Space bar's other icons and has no wash behind
// it, so the accent circle alone is what catches the eye.
constexpr int kButtonSize = 28;
constexpr int kGlyphSize = 22;

constexpr SkColor kAccent = SkColorSetRGB(0x6E, 0xA1, 0xF7);
constexpr SkColor kAccentBright = SkColorSetRGB(0xA8, 0xC7, 0xFB);

// How far the arrow lifts at the peak of a pulse, and the ring's reach: from
// the circle out to the button's edge.
constexpr float kArrowLift = 2.0f;
constexpr float kRingStartRadius = kGlyphSize * 10.0f / 24.0f;
constexpr float kRingEndRadius = kButtonSize / 2.0f - 0.75f;
constexpr int kRingAlpha = 0xCC;

constexpr base::TimeDelta kPulseDuration = base::Milliseconds(1200);
constexpr base::TimeDelta kPulseRest = base::Milliseconds(2400);

// Lucide "circle-arrow-up", in two parts so the arrow can move on its own.
constexpr char kCirclePath[] = "M22 12a10 10 0 1 1-20 0 10 10 0 0 1 20 0z";
constexpr char kArrowPath[] =
    "M16 12l-4-4-4 4"
    "M12 16V8";


}  // namespace

UpdateButton::UpdateButton()
    : views::Button(base::BindRepeating(&CheckForUpdates)),
      pulse_({{kPulseDuration, gfx::Tween::LINEAR, 0.0, 1.0},
              {kPulseRest, gfx::Tween::LINEAR, 1.0, 1.0}}) {
  SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
  pulse_.set_delegate(this);
  AddUpdaterObserver(this);
  OnWaitingUpdateChanged();
}

UpdateButton::~UpdateButton() {
  RemoveUpdaterObserver(this);
}

void UpdateButton::PaintButtonContents(gfx::Canvas* canvas) {
  const bool hovered = GetState() == views::Button::STATE_HOVERED ||
                       GetState() == views::Button::STATE_PRESSED;
  // Rises and falls once over the pulse; zero between pulses.
  const double progress = pulse_.is_animating() ? pulse_.GetCurrentValue() : 1;
  const float swell =
      static_cast<float>(std::sin(std::numbers::pi * progress));

  // Brightens on hover, and towards the peak of each pulse.
  const SkColor glyph_color =
      hovered ? kAccentBright
              : color_utils::AlphaBlend(kAccentBright, kAccent, swell);

  gfx::Rect glyph(GetLocalBounds());
  glyph.ClampToCenteredSize(gfx::Size(kGlyphSize, kGlyphSize));

  if (progress < 1) {
    const double eased =
        gfx::Tween::CalculateValue(gfx::Tween::EASE_OUT, progress);
    cc::PaintFlags ring;
    ring.setAntiAlias(true);
    ring.setStyle(cc::PaintFlags::kStroke_Style);
    ring.setStrokeWidth(1.5f);
    ring.setColor(
        SkColorSetA(kAccent, std::lround((1 - eased) * kRingAlpha)));
    canvas->DrawCircle(
        gfx::RectF(glyph).CenterPoint(),
        kRingStartRadius + eased * (kRingEndRadius - kRingStartRadius), ring);
  }

  PaintLucidePathData(canvas, glyph, kCirclePath, glyph_color);
  gfx::ScopedCanvas lifted(canvas);
  canvas->sk_canvas()->translate(0, -swell * kArrowLift);
  PaintLucidePathData(canvas, glyph, kArrowPath, glyph_color);
}

void UpdateButton::AnimationProgressed(const gfx::Animation* animation) {
  if (animation != &pulse_) {
    views::Button::AnimationProgressed(animation);
    return;
  }
  // Between pulses nothing moves, so one paint at rest is enough.
  const bool resting = pulse_.current_part_index() > 0;
  if (!resting || !painted_rest_) {
    SchedulePaint();
  }
  painted_rest_ = resting;
}

void UpdateButton::AnimationEnded(const gfx::Animation* animation) {
  if (animation != &pulse_) {
    views::Button::AnimationEnded(animation);
    return;
  }
  SchedulePaint();
}

void UpdateButton::OnWaitingUpdateChanged() {
  const std::u16string version = WaitingUpdateVersion();
  SetVisible(!version.empty());
  if (version.empty()) {
    pulse_.Stop();
    return;
  }
  if (!pulse_.is_animating() && !gfx::Animation::PrefersReducedMotion()) {
    pulse_.Start();
  }
  // Says what a click shows: Sparkle's window opens on the release notes, with
  // the choice to install, remind later or skip.
  SetTooltipText(base::StrCat(
      {u"Avora ", version, u" is available.\nClick to see what\u2019s new."}));
  GetViewAccessibility().SetName(base::StrCat({u"Update to Avora ", version}));
  GetViewAccessibility().SetDescription(u"Shows the release notes");
}

BEGIN_METADATA(UpdateButton)
END_METADATA

}  // namespace avora
