// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_loading_bar_view.h"

#include <algorithm>
#include <cmath>

#include "base/functional/bind.h"
#include "base/notreached.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace avora {

namespace {

// ── Colour ramp ─────────────────────────────────────────────────────────────
// An aurora: Avora's accent blue rising through cyan into violet, tipped with
// an almost-white core so the leading edge reads as light rather than paint.
// The ramp is anchored to the window width, not to the filled width, so the
// colours are uncovered as the page loads instead of stretching and shifting
// under the eye.
constexpr SkColor4f kAuroraStart{0.357f, 0.545f, 1.000f, 1.f};  // #5B8BFF
constexpr SkColor4f kAuroraMid{0.408f, 0.878f, 0.973f, 1.f};    // #68E0F8
constexpr SkColor4f kAuroraEnd{0.655f, 0.545f, 0.980f, 1.f};    // #A78BFA
constexpr SkColor4f kHeadCore{0.929f, 0.969f, 1.000f, 1.f};     // #EDF7FF

// ── Metrics ─────────────────────────────────────────────────────────────────
// Radius of the bloom carried at the leading edge.  Wider than the bar is tall
// by an order of magnitude -- that mismatch is what makes it read as light
// spilling off the end rather than as a rounded cap.
constexpr float kHeadGlowRadius = 26.f;
// The travelling specular highlight.
constexpr float kSpecularWidth = 150.f;
constexpr double kSpecularPeriod = 1.5;  // seconds
// The waiting comet: a fraction of the window, floored so a narrow window
// still gets a comet rather than a dot.
constexpr float kCometWidthRatio = 0.26f;
constexpr float kCometMinWidth = 140.f;
constexpr double kCometPeriod = 1.15;  // seconds

// ── Timing ──────────────────────────────────────────────────────────────────
constexpr double kFadeInSeconds = 0.12;
constexpr double kFadeOutSeconds = 0.32;
// Cross-fade from the waiting comet to the determinate ribbon.
constexpr double kBlendSeconds = 0.20;
// The single pulse when a load lands.
constexpr double kBloomSeconds = 0.42;
// Time constants for the exponential ease that chases the target progress.
// Finishing is much stiffer so the ribbon visibly snaps home.
constexpr double kEaseTau = 0.18;
constexpr double kFinishTau = 0.07;
// The trickle floor rises toward this share of the bar with this time
// constant, so a load that reports nothing for seconds still crawls forward.
constexpr double kTrickleCeiling = 0.9;
constexpr double kTrickleTau = 2.2;
// Progress below which a load has nothing real to show yet and the comet
// carries the wait instead.
constexpr double kMeaningfulProgress = 0.05;
// The determinate phase never fills the last slice: reaching the right edge is
// reserved for the moment the page is actually done.
constexpr double kLoadingCeiling = 0.985;
// Close enough to full to start the outro.
constexpr double kFullEnough = 0.995;

constexpr base::TimeDelta kFrameInterval = base::Milliseconds(16);
// A frame this long means the compositor stalled or the machine slept; clamp
// rather than jumping every animation forward by the whole gap.
constexpr double kMaxFrameSeconds = 0.1;

SkColor4f WithAlpha(SkColor4f color, float alpha) {
  color.fA = std::clamp(alpha, 0.f, 1.f);
  return color;
}

// The aurora ramp across [|x0|, |x1|], at |alpha| opacity.
sk_sp<cc::PaintShader> MakeAuroraShader(float x0, float x1, float alpha) {
  const SkPoint points[2] = {SkPoint::Make(x0, 0), SkPoint::Make(x1, 0)};
  const SkColor4f colors[3] = {WithAlpha(kAuroraStart, alpha),
                               WithAlpha(kAuroraMid, alpha),
                               WithAlpha(kAuroraEnd, alpha)};
  const SkScalar positions[3] = {0.f, 0.55f, 1.f};
  return cc::PaintShader::MakeLinearGradient(points, colors, positions, 3,
                                             SkTileMode::kClamp);
}

// The comet: transparent at the tail, brightening into a white-hot head.
sk_sp<cc::PaintShader> MakeCometShader(float tail, float head, float alpha) {
  const SkPoint points[2] = {SkPoint::Make(tail, 0), SkPoint::Make(head, 0)};
  const SkColor4f colors[4] = {
      WithAlpha(kAuroraStart, 0.f), WithAlpha(kAuroraStart, 0.35f * alpha),
      WithAlpha(kAuroraMid, 0.95f * alpha), WithAlpha(kHeadCore, alpha)};
  const SkScalar positions[4] = {0.f, 0.45f, 0.85f, 1.f};
  return cc::PaintShader::MakeLinearGradient(points, colors, positions, 4,
                                             SkTileMode::kClamp);
}

// Paints the light the bar casts below itself.  |shader| supplies the
// horizontal colour ramp (carrying its own alpha) and the glow is faded out
// vertically by multiplying it down inside a temporary layer -- two ramps at
// once, which a single gradient cannot express.
void PaintUnderGlow(gfx::Canvas* canvas,
                    const gfx::RectF& band,
                    sk_sp<cc::PaintShader> shader) {
  if (band.IsEmpty()) {
    return;
  }

  canvas->SaveLayerAlpha(SK_AlphaOPAQUE, gfx::ToEnclosingRect(band));

  cc::PaintFlags color_flags;
  color_flags.setStyle(cc::PaintFlags::kFill_Style);
  color_flags.setShader(std::move(shader));
  canvas->DrawRect(band, color_flags);

  const SkPoint points[2] = {SkPoint::Make(0, band.y()),
                             SkPoint::Make(0, band.bottom())};
  const SkColor4f fade[2] = {{0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 0.f, 0.f}};
  const SkScalar positions[2] = {0.f, 1.f};
  cc::PaintFlags fade_flags;
  fade_flags.setBlendMode(SkBlendMode::kDstIn);
  fade_flags.setShader(cc::PaintShader::MakeLinearGradient(
      points, fade, positions, 2, SkTileMode::kClamp));
  canvas->DrawRect(band, fade_flags);

  canvas->Restore();
}

// Paints the bloom riding the leading edge at |head_x|.
void PaintHead(gfx::Canvas* canvas, float head_x, float height, float alpha) {
  const SkColor4f colors[3] = {WithAlpha(kHeadCore, 0.75f * alpha),
                               WithAlpha(kAuroraMid, 0.30f * alpha),
                               WithAlpha(kAuroraMid, 0.f)};
  const SkScalar positions[3] = {0.f, 0.32f, 1.f};

  cc::PaintFlags flags;
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setShader(cc::PaintShader::MakeRadialGradient(
      SkPoint::Make(head_x, AvoraLoadingBarView::kBarHeight * 0.5f),
      kHeadGlowRadius, colors, positions, 3, SkTileMode::kClamp));
  canvas->DrawRect(
      gfx::RectF(head_x - kHeadGlowRadius, 0.f, 2.f * kHeadGlowRadius, height),
      flags);
}

}  // namespace

AvoraLoadingBarView::AvoraLoadingBarView() {
  // The bar is drawn over the sidebar header, which is layer-backed; a layer of
  // its own is what keeps it on top of that rather than behind it.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);

  // Purely decorative: never take a click meant for the window's top edge, and
  // stay out of the accessibility tree -- load state is already announced.
  SetCanProcessEventsWithinSubtree(false);
  GetViewAccessibility().SetIsIgnored(true);
}

AvoraLoadingBarView::~AvoraLoadingBarView() = default;

void AvoraLoadingBarView::SetWebContents(content::WebContents* contents) {
  if (web_contents() == contents) {
    return;
  }

  Observe(contents);

  // Adopt a load already in flight so switching to a loading tab shows where
  // that tab has got to, and switching away drops the old tab's animation
  // instead of trailing it over the new page.
  Reset();
  if (contents && contents->IsLoading()) {
    StartLoad(contents->GetLoadProgress());
  } else {
    // Reset() stops the frame clock, so nothing else would repaint away the
    // outgoing tab's last frame.
    SchedulePaint();
  }
}

void AvoraLoadingBarView::DidStartLoading() {
  StartLoad(0.0);
}

void AvoraLoadingBarView::LoadProgressChanged(double progress) {
  if (phase_ == Phase::kIdle) {
    StartLoad(progress);
    return;
  }
  if (phase_ == Phase::kFinishing) {
    return;
  }

  // Monotonic: a second navigation in the same tab restarts the bar through
  // DidStartLoading, so within one load a lower number is noise.
  reported_progress_ =
      std::max(reported_progress_, std::clamp(progress, 0.0, 1.0));

  if (phase_ == Phase::kWaiting && reported_progress_ >= kMeaningfulProgress) {
    phase_ = Phase::kLoading;
  }
  if (reported_progress_ >= 1.0) {
    FinishLoad();
  }
}

void AvoraLoadingBarView::DidStopLoading() {
  FinishLoad();
}

void AvoraLoadingBarView::WebContentsDestroyed() {
  Observe(nullptr);
  Reset();
  SchedulePaint();
}

void AvoraLoadingBarView::StartLoad(double initial_progress) {
  const bool was_running = phase_ != Phase::kIdle;

  reported_progress_ = std::clamp(initial_progress, 0.0, 1.0);
  drawn_progress_ = 0.0;
  loading_seconds_ = 0.0;
  bloom_ = 0.0;
  fading_out_ = false;
  if (!was_running) {
    opacity_ = 0.0;
    cycle_seconds_ = 0.0;
  }

  // With reduced motion the sweeping comet is exactly what the setting is
  // asking us not to do, so those loads go straight to the determinate ribbon,
  // which only ever eases forward.
  const bool skip_comet = gfx::Animation::PrefersReducedMotion() ||
                          reported_progress_ >= kMeaningfulProgress;
  phase_ = skip_comet ? Phase::kLoading : Phase::kWaiting;
  ribbon_blend_ = skip_comet ? 1.0 : 0.0;

  EnsureTicking();
  SchedulePaint();
}

void AvoraLoadingBarView::FinishLoad() {
  if (phase_ == Phase::kIdle || phase_ == Phase::kFinishing) {
    return;
  }
  phase_ = Phase::kFinishing;
  reported_progress_ = 1.0;
  EnsureTicking();
}

void AvoraLoadingBarView::Reset() {
  phase_ = Phase::kIdle;
  reported_progress_ = 0.0;
  drawn_progress_ = 0.0;
  loading_seconds_ = 0.0;
  ribbon_blend_ = 0.0;
  opacity_ = 0.0;
  bloom_ = 0.0;
  fading_out_ = false;
  frame_timer_.Stop();
}

void AvoraLoadingBarView::EnsureTicking() {
  if (frame_timer_.IsRunning()) {
    return;
  }
  last_frame_ = base::TimeTicks::Now();
  frame_timer_.Start(FROM_HERE, kFrameInterval,
                     base::BindRepeating(&AvoraLoadingBarView::OnFrame,
                                         base::Unretained(this)));
}

void AvoraLoadingBarView::OnFrame() {
  const base::TimeTicks now = base::TimeTicks::Now();
  const double dt =
      std::clamp((now - last_frame_).InSecondsF(), 0.0, kMaxFrameSeconds);
  last_frame_ = now;

  if (!Advance(dt)) {
    frame_timer_.Stop();
  }
  SchedulePaint();
}

bool AvoraLoadingBarView::Advance(double dt) {
  cycle_seconds_ += dt;

  switch (phase_) {
    case Phase::kIdle:
      return false;

    case Phase::kWaiting:
      opacity_ = std::min(1.0, opacity_ + dt / kFadeInSeconds);
      return true;

    case Phase::kLoading: {
      opacity_ = std::min(1.0, opacity_ + dt / kFadeInSeconds);
      ribbon_blend_ = std::min(1.0, ribbon_blend_ + dt / kBlendSeconds);
      loading_seconds_ += dt;

      const double trickle =
          kTrickleCeiling * (1.0 - std::exp(-loading_seconds_ / kTrickleTau));
      const double target =
          std::min(kLoadingCeiling, std::max(reported_progress_, trickle));
      drawn_progress_ +=
          (target - drawn_progress_) * (1.0 - std::exp(-dt / kEaseTau));
      return true;
    }

    case Phase::kFinishing: {
      ribbon_blend_ = std::min(1.0, ribbon_blend_ + dt / kBlendSeconds);
      drawn_progress_ +=
          (1.0 - drawn_progress_) * (1.0 - std::exp(-dt / kFinishTau));

      if (!fading_out_) {
        opacity_ = std::min(1.0, opacity_ + dt / kFadeInSeconds);
        // Hold the full-width ribbon for exactly one bloom, then leave.  A
        // load that finished before the bar even faded in still gets the whole
        // outro, so instant navigations read as a deliberate sweep rather than
        // a flicker.
        if (drawn_progress_ >= kFullEnough && opacity_ >= 1.0) {
          fading_out_ = true;
          bloom_ = 1.0;
        }
        return true;
      }

      bloom_ = std::max(0.0, bloom_ - dt / kBloomSeconds);
      opacity_ -= dt / kFadeOutSeconds;
      if (opacity_ <= 0.0) {
        Reset();
        return false;
      }
      return true;
    }
  }
  NOTREACHED();
}

void AvoraLoadingBarView::OnPaint(gfx::Canvas* canvas) {
  if (phase_ == Phase::kIdle || opacity_ <= 0.0 || width() <= 0) {
    return;
  }

  const float alpha = static_cast<float>(std::clamp(opacity_, 0.0, 1.0));
  const float blend = static_cast<float>(std::clamp(ribbon_blend_, 0.0, 1.0));

  if (blend < 1.f) {
    PaintComet(canvas, alpha * (1.f - blend));
  }
  if (blend > 0.f) {
    PaintRibbon(canvas, alpha * blend);
  }
}

void AvoraLoadingBarView::PaintComet(gfx::Canvas* canvas, float alpha) const {
  const float w = width();
  const float span = std::max(kCometMinWidth, w * kCometWidthRatio);

  // Eased rather than linear: the comet gathers speed in the middle of the
  // window and slows at the edges, which is what keeps a loop from reading as
  // a machine part going round.
  const float cycle =
      static_cast<float>(std::fmod(cycle_seconds_, kCometPeriod) / kCometPeriod);
  const float eased = static_cast<float>(
      gfx::Tween::CalculateValue(gfx::Tween::EASE_IN_OUT, cycle));
  const float head = eased * (w + span);
  const float tail = head - span;

  PaintUnderGlow(canvas,
                 gfx::RectF(std::max(0.f, tail), kBarHeight,
                            std::min(w, head) - std::max(0.f, tail), kGlowHeight),
                 MakeCometShader(tail, head, 0.45f * alpha));

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setShader(MakeCometShader(tail, head, alpha));
  canvas->DrawRoundRect(gfx::RectF(tail, 0.f, span, kBarHeight),
                        kBarHeight * 0.5f, flags);

  PaintHead(canvas, head, height(), alpha);
}

void AvoraLoadingBarView::PaintRibbon(gfx::Canvas* canvas, float alpha) const {
  const float w = width();
  const float head =
      std::clamp(static_cast<float>(drawn_progress_), 0.f, 1.f) * w;
  if (head < 1.f) {
    return;
  }

  const gfx::RectF bar(0.f, 0.f, head, kBarHeight);

  PaintUnderGlow(canvas, gfx::RectF(0.f, kBarHeight, head, kGlowHeight),
                 MakeAuroraShader(0.f, w, 0.42f * alpha));

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setShader(MakeAuroraShader(0.f, w, alpha));
  canvas->DrawRoundRect(bar, kBarHeight * 0.5f, flags);

  // A highlight sweeping along the lit part, faster than the fill grows.  It
  // is what makes a bar that is waiting on the network still look powered.
  if (!gfx::Animation::PrefersReducedMotion() && !fading_out_) {
    const float cycle = static_cast<float>(
        std::fmod(cycle_seconds_, kSpecularPeriod) / kSpecularPeriod);
    const float start = -kSpecularWidth + cycle * (w + 2.f * kSpecularWidth);

    const SkPoint points[2] = {SkPoint::Make(start, 0),
                               SkPoint::Make(start + kSpecularWidth, 0)};
    const SkColor4f colors[3] = {WithAlpha(kHeadCore, 0.f),
                                 WithAlpha(kHeadCore, 0.5f * alpha),
                                 WithAlpha(kHeadCore, 0.f)};
    const SkScalar positions[3] = {0.f, 0.5f, 1.f};

    cc::PaintFlags specular;
    specular.setStyle(cc::PaintFlags::kFill_Style);
    specular.setShader(cc::PaintShader::MakeLinearGradient(
        points, colors, positions, 3, SkTileMode::kClamp));

    canvas->Save();
    canvas->ClipRect(bar);
    canvas->DrawRect(gfx::RectF(start, 0.f, kSpecularWidth, kBarHeight),
                     specular);
    canvas->Restore();
  }

  PaintHead(canvas, head, height(), alpha);

  // The landing: one white pulse across the whole ribbon as it reaches the
  // right edge, decaying while the bar fades away under it.
  if (bloom_ > 0.0) {
    const float bloom = static_cast<float>(bloom_);
    cc::PaintFlags bloom_flags;
    bloom_flags.setAntiAlias(true);
    bloom_flags.setStyle(cc::PaintFlags::kFill_Style);
    bloom_flags.setColor(WithAlpha(kHeadCore, 0.55f * bloom * alpha));
    canvas->DrawRoundRect(bar, kBarHeight * 0.5f, bloom_flags);

    PaintUnderGlow(canvas, gfx::RectF(0.f, kBarHeight, head, kGlowHeight),
                   MakeAuroraShader(0.f, w, 0.5f * bloom * alpha));
  }
}

BEGIN_METADATA(AvoraLoadingBarView)
END_METADATA

}  // namespace avora
