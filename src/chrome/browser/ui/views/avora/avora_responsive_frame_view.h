// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_RESPONSIVE_FRAME_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_RESPONSIVE_FRAME_VIEW_H_

#include <array>
#include <optional>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/view.h"
#include "ui/views/view_targeter_delegate.h"

namespace content {
class HostZoomMap;
class WebContents;
}  // namespace content

namespace views {
class Label;
}  // namespace views

namespace avora {

// Responsive mode: the page shrinks into a frame centred in the contents
// area, with handles on its sides and bottom to drag it to any size and a
// readout of the size the page sees.  It is for checking a site's breakpoints
// without DevTools: the page really is laid out at that width, so media
// queries, container queries and JS reading innerWidth all respond.
//
// The mode belongs to the tab, so a tab left at phone width stays there while
// its neighbours fill the window.  MultiContentsView owns the frame view and
// places the page container where ComputeResponsiveLayout() says.

// ── Per-tab state ───────────────────────────────────────────────────────────

// Requested page size in DIPs.  0 on an axis means "fill the space there is".
struct ResponsiveSize {
  int width = 0;
  int height = 0;

  bool operator==(const ResponsiveSize&) const = default;
};

// Whether |contents| is in responsive mode, and at what size.
std::optional<ResponsiveSize> GetResponsiveSize(
    content::WebContents* contents);
// Puts |contents| in responsive mode at |size|, or takes it out with nullopt.
void SetResponsiveSize(content::WebContents* contents,
                       std::optional<ResponsiveSize> size);

// ── Geometry ────────────────────────────────────────────────────────────────

inline constexpr int kResponsiveToolbarHeight = 44;
inline constexpr int kResponsiveSideGutter = 28;
inline constexpr int kResponsiveBottomGutter = 28;
inline constexpr int kResponsiveHandleThickness = 14;
inline constexpr int kResponsiveHandleGap = 4;
inline constexpr int kResponsiveMinPageSize = 200;

struct ResponsiveLayout {
  gfx::Rect toolbar;
  gfx::Rect page;
  gfx::Rect left_handle;
  gfx::Rect right_handle;
  gfx::Rect bottom_handle;
  // The largest page that fits.
  gfx::Size max_page;
};

// Where everything goes inside |available|, for a page of |requested| size.
// The page is centred horizontally and hangs from just under the toolbar;
// each axis is clamped to what fits and filled when |requested| is 0 there.
ResponsiveLayout ComputeResponsiveLayout(const gfx::Rect& available,
                                         const ResponsiveSize& requested);

// Widths, in CSS pixels, that a dragged edge snaps to: the common breakpoints
// (Tailwind's, which match most frameworks closely enough) and phone widths.
inline constexpr std::array<int, 8> kResponsiveSnapWidths = {
    375, 390, 430, 640, 768, 1024, 1280, 1536};
inline constexpr int kResponsiveSnapDistance = 8;

// |css_width| moved onto a snap width when it is within the snap distance of
// one, else unchanged.
int SnapResponsiveWidth(int css_width);

// The breakpoint label for a page |css_width| wide: "xs", "sm", "md", "lg",
// "xl" or "2xl", using the same min-width breakpoints as the snap widths.
const char* ResponsiveBreakpointName(int css_width);

// ── The view ────────────────────────────────────────────────────────────────

// Sits above the page container, which keeps the whole contents area so that
// docked DevTools stay where they are; the frame paints around the page rather
// than over it, and only its toolbar and handles take events.
class AvoraResponsiveFrameView : public views::View,
                                 public views::ViewTargeterDelegate {
  METADATA_HEADER(AvoraResponsiveFrameView, views::View)

 public:
  class Delegate {
   public:
    // The tab the frame is around.  May be null between tabs.
    virtual content::WebContents* GetResponsiveWebContents() = 0;
    // The requested size changed; the page needs laying out again.
    virtual void OnResponsiveSizeChanged() = 0;

   protected:
    virtual ~Delegate() = default;
  };

  explicit AvoraResponsiveFrameView(Delegate* delegate);
  AvoraResponsiveFrameView(const AvoraResponsiveFrameView&) = delete;
  AvoraResponsiveFrameView& operator=(const AvoraResponsiveFrameView&) = delete;
  ~AvoraResponsiveFrameView() override;

  // Re-reads the tab's size and zoom: after a tab switch, a toggle, or a
  // change made from outside the view.
  void Refresh();

  // Applies a requested size from inside the view (a drag, a preset) and asks
  // the delegate to lay the page out again.
  void SetRequestedSize(const ResponsiveSize& size, bool dragging = false);

  // Leaves responsive mode for the current tab.
  void Exit();

  // The CSS-to-DIP factor of the current tab: its zoom.
  double ZoomFactor() const;

  const ResponsiveLayout& current_layout() const { return layout_; }
  const ResponsiveSize& requested_size() const { return requested_; }

  // views::ViewTargeterDelegate:
  bool DoesIntersectRect(const views::View* target,
                         const gfx::Rect& rect) const override;

  // views::View:
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;
  void OnThemeChanged() override;

 private:
  void UpdateReadout();
  void ObserveZoomFor(content::WebContents* contents);
  void ApplyPreset(int css_width, int css_height);
  void Rotate();

  const raw_ptr<Delegate> delegate_;
  ResponsiveSize requested_;
  ResponsiveLayout layout_;
  bool dragging_ = false;

  raw_ptr<views::View> toolbar_ = nullptr;
  raw_ptr<views::Label> size_label_ = nullptr;
  raw_ptr<views::Label> breakpoint_label_ = nullptr;
  raw_ptr<views::View> presets_ = nullptr;
  raw_ptr<views::View> left_handle_ = nullptr;
  raw_ptr<views::View> right_handle_ = nullptr;
  raw_ptr<views::View> bottom_handle_ = nullptr;

  raw_ptr<content::HostZoomMap> observed_zoom_map_ = nullptr;
  base::CallbackListSubscription zoom_subscription_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_RESPONSIVE_FRAME_VIEW_H_
