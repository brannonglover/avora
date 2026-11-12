// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_RESIZE_READOUT_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_RESIZE_READOUT_VIEW_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

namespace views {
class Label;
class WebView;
}  // namespace views

namespace avora {

// The page's size while the window is being resized by its edges: a pill
// centred over the page reading, say, "390 × 844" and the breakpoint that
// width falls in.  It is how a site's responsive layout gets checked in
// Avora -- pull the window down to a phone's width and read the numbers --
// so it shows only during a live resize, and fades shortly after.
//
// The size is the page's own: CSS pixels, so page zoom is allowed for, and
// the page alone, not the window around it.
class AvoraResizeReadoutView : public views::View, public views::ViewObserver {
  METADATA_HEADER(AvoraResizeReadoutView, views::View)

 public:
  AvoraResizeReadoutView();
  AvoraResizeReadoutView(const AvoraResizeReadoutView&) = delete;
  AvoraResizeReadoutView& operator=(const AvoraResizeReadoutView&) = delete;
  ~AvoraResizeReadoutView() override;

  // A live resize began: show |page|'s size, and follow it as it changes.
  void StartTracking(views::WebView* page);
  // The live resize ended: keep the final size up for a moment, then fade.
  void StopTracking();

  // The text shown, for tests.
  std::u16string GetSizeTextForTesting() const;
  std::u16string GetBreakpointTextForTesting() const;

  // The size in CSS pixels of a page |dip_size| DIPs at |zoom_factor|, and
  // the breakpoint label for |css_width| ("xs" below 640, then sm, md, lg,
  // xl, 2xl at 640, 768, 1024, 1280, 1536).
  static gfx::Size CssSize(const gfx::Size& dip_size, double zoom_factor);
  static std::u16string BreakpointLabel(int css_width);

  // views::View:
  void OnThemeChanged() override;

 private:
  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* observed_view) override;
  void OnViewIsDeleting(views::View* observed_view) override;

  // Re-reads the page's size and re-centres the pill over it.
  void Update();
  void FadeOut();

  raw_ptr<views::WebView> page_ = nullptr;
  raw_ptr<views::Label> size_label_ = nullptr;
  raw_ptr<views::Label> breakpoint_label_ = nullptr;
  base::OneShotTimer linger_timer_;
  base::ScopedObservation<views::View, views::ViewObserver> page_observation_{
      this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_RESIZE_READOUT_VIEW_H_
