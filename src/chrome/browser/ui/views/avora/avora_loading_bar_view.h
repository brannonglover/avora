// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LOADING_BAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LOADING_BAR_VIEW_H_

#include "base/time/time.h"
#include "base/timer/timer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace content {
class WebContents;
}

namespace gfx {
class Canvas;
}

namespace avora {

// Window-level page-load indicator: a thin luminous ribbon pinned to the very
// top edge of the browser window, spanning its full width.  It replaces the
// spinner Chromium paints over the favicon, so load progress reads as a
// property of the window rather than of one tab row in the sidebar.
//
// The bar follows the *active* tab only -- a window has one top edge, so
// showing background loads there would be a lie about which page is moving.
// Switching tabs adopts the new tab's load in progress, or clears the bar.
//
// Three visual phases, all driven from a single elapsed-time clock so they
// stay in step:
//
//   waiting   A comet sweeps the window edge while the server is still
//             thinking and there is no real progress to show yet.
//   loading   The comet cross-fades into a determinate ribbon whose colour
//             ramp is anchored to the window, so the aurora is *revealed*
//             left to right instead of being squeezed into the filled width.
//             A specular highlight sweeps along it, and a soft bloom rides
//             the leading edge.
//   finishing The ribbon races to full width, blooms once, and fades out.
//
// Real progress can stall for seconds at a time, so the determinate phase
// also carries a "trickle" floor that creeps toward 90%: the bar keeps
// moving whenever the network does not, and never moves backwards.
//
// The view is decorative: it never takes focus, never handles events, and is
// hidden from accessibility (page load state is already announced).
class AvoraLoadingBarView : public views::View,
                            public content::WebContentsObserver {
  METADATA_HEADER(AvoraLoadingBarView, views::View)

 public:
  // The lit ribbon itself.
  static constexpr int kBarHeight = 3;
  // Space reserved below the ribbon for its glow.  The view paints to a layer,
  // which clips at its bounds, so the glow needs room inside the view.
  static constexpr int kGlowHeight = 12;
  // Height the layout should give the view.
  static constexpr int kPreferredHeight = kBarHeight + kGlowHeight;

  AvoraLoadingBarView();
  AvoraLoadingBarView(const AvoraLoadingBarView&) = delete;
  AvoraLoadingBarView& operator=(const AvoraLoadingBarView&) = delete;
  ~AvoraLoadingBarView() override;

  // Points the bar at the window's active tab.  Passing a tab that is already
  // loading adopts its progress mid-flight; anything else clears the bar
  // immediately, without a fade, so a tab switch never trails the old tab's
  // animation over the new page.
  void SetWebContents(content::WebContents* contents);

  // content::WebContentsObserver:
  void DidStartLoading() override;
  void DidStopLoading() override;
  void LoadProgressChanged(double progress) override;
  void WebContentsDestroyed() override;

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  enum class Phase {
    kIdle,       // Nothing loading; the view paints nothing.
    kWaiting,    // Load started, no meaningful progress yet: comet sweep.
    kLoading,    // Determinate ribbon, possibly still cross-fading in.
    kFinishing,  // Racing to full width, then blooming and fading out.
  };

  // Enters |kWaiting| (or |kLoading| if |initial_progress| is already
  // meaningful) and starts the frame clock.
  void StartLoad(double initial_progress);
  void FinishLoad();
  // Clears everything without an outro; used on tab switches and teardown.
  void Reset();

  void EnsureTicking();
  void OnFrame();
  // Advances every animated quantity by |dt| seconds and returns whether the
  // bar still has something to draw.
  bool Advance(double dt);

  // Painting helpers.  |alpha| is the phase's own opacity in [0,1]; the
  // overall fade is applied on top of it by OnPaint.
  void PaintComet(gfx::Canvas* canvas, float alpha) const;
  void PaintRibbon(gfx::Canvas* canvas, float alpha) const;

  Phase phase_ = Phase::kIdle;

  // Highest progress the renderer has reported for this load, in [0,1].
  // Monotonic: a reported value below the current one is ignored, so a
  // same-document navigation cannot yank the bar backwards.
  double reported_progress_ = 0.0;
  // Where the ribbon is actually drawn; eases toward the target every frame.
  double drawn_progress_ = 0.0;
  // Seconds spent in the determinate phase, feeding the trickle floor.
  double loading_seconds_ = 0.0;
  // Free-running clock for the comet and specular loops.
  double cycle_seconds_ = 0.0;
  // Cross-fade from the comet to the ribbon, in [0,1].
  double ribbon_blend_ = 0.0;
  // Overall opacity, in [0,1]; ramps up on start and down on finish.
  double opacity_ = 0.0;
  // The single bright pulse at the end of a load, decaying from 1 to 0.
  double bloom_ = 0.0;
  // Set once the ribbon has reached full width and the outro has begun.
  bool fading_out_ = false;

  base::TimeTicks last_frame_;
  base::RepeatingTimer frame_timer_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LOADING_BAR_VIEW_H_
