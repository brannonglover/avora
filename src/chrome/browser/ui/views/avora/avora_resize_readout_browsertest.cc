// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_resize_readout_view.h"

#include <string>

#include "base/strings/string_number_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/remote_cocoa/app_shim/avora_centred_resize.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace avora {

namespace {

// ── Centred resize ──────────────────────────────────────────────────────────

TEST(AvoraCentredResizeTest, DraggedEdgeIsMirroredAroundTheCentre) {
  // A window from 300 to 700 (centre 500); the right edge dragged in to 650.
  remote_cocoa::CentredSpan span =
      remote_cocoa::CentredResizeSpan(650, 500, 0, 0);
  EXPECT_EQ(350, span.origin);
  EXPECT_EQ(300, span.length);

  // The left edge dragged out to 250 grows the right side to 750.
  span = remote_cocoa::CentredResizeSpan(250, 500, 0, 0);
  EXPECT_EQ(250, span.origin);
  EXPECT_EQ(500, span.length);
}

TEST(AvoraCentredResizeTest, HeldBetweenMinimumAndMaximum) {
  remote_cocoa::CentredSpan span =
      remote_cocoa::CentredResizeSpan(520, 500, 400, 0);
  EXPECT_EQ(400, span.length);
  EXPECT_EQ(300, span.origin);

  span = remote_cocoa::CentredResizeSpan(2000, 500, 0, 1000);
  EXPECT_EQ(1000, span.length);
  EXPECT_EQ(0, span.origin);
}

// ── Readout text ────────────────────────────────────────────────────────────

TEST(AvoraResizeReadoutTest, CssSizeAllowsForZoom) {
  EXPECT_EQ(gfx::Size(390, 844),
            AvoraResizeReadoutView::CssSize(gfx::Size(390, 844), 1.0));
  EXPECT_EQ(gfx::Size(390, 844),
            AvoraResizeReadoutView::CssSize(gfx::Size(429, 928), 1.1));
}

TEST(AvoraResizeReadoutTest, BreakpointLabels) {
  EXPECT_EQ(u"xs  \u00B7  < 640", AvoraResizeReadoutView::BreakpointLabel(390));
  EXPECT_EQ(u"sm  \u00B7  640 \u2013 767",
            AvoraResizeReadoutView::BreakpointLabel(640));
  EXPECT_EQ(u"md  \u00B7  768 \u2013 1023",
            AvoraResizeReadoutView::BreakpointLabel(1023));
  EXPECT_EQ(u"lg  \u00B7  1024 \u2013 1279",
            AvoraResizeReadoutView::BreakpointLabel(1024));
  EXPECT_EQ(u"2xl  \u00B7  \u2265 1536",
            AvoraResizeReadoutView::BreakpointLabel(1920));
}

// ── In a window ─────────────────────────────────────────────────────────────

class AvoraResizeReadoutBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), GURL("data:text/html,<title>t</title><h1>page</h1>")));
  }

 protected:
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  MultiContentsView* contents_area() {
    return browser_view()->multi_contents_view();
  }
  AvoraResizeReadoutView* readout() {
    return contents_area()->avora_resize_readout_for_testing();
  }
  ContentsWebView* page() { return contents_area()->GetActiveContentsView(); }

  void ResizeWindow(int width, int height) {
    browser_view()->GetWidget()->SetBounds(gfx::Rect(100, 100, width, height));
    views::test::RunScheduledLayout(browser_view());
  }

  std::u16string ExpectedSizeText() {
    return base::NumberToString16(page()->width()) + u" \u00D7 " +
           base::NumberToString16(page()->height());
  }
};

IN_PROC_BROWSER_TEST_F(AvoraResizeReadoutBrowserTest,
                       ShowsThePageSizeOnlyWhileResizing) {
  ResizeWindow(1400, 900);
  EXPECT_FALSE(readout()->GetVisible());

  // Resizing outside a live resize (a restore, zoom, fullscreen) shows
  // nothing.
  ResizeWindow(1300, 900);
  EXPECT_FALSE(readout()->GetVisible());

  contents_area()->SetAvoraLiveResizing(true);
  EXPECT_TRUE(readout()->GetVisible());
  EXPECT_EQ(ExpectedSizeText(), readout()->GetSizeTextForTesting());

  // Follows the page as the window changes size, and stays over it.
  ResizeWindow(900, 800);
  EXPECT_EQ(ExpectedSizeText(), readout()->GetSizeTextForTesting());
  const gfx::Rect page_bounds = views::View::ConvertRectToTarget(
      page(), contents_area(), page()->GetLocalBounds());
  EXPECT_NEAR(page_bounds.CenterPoint().x(),
              readout()->bounds().CenterPoint().x(), 1);
  EXPECT_NEAR(page_bounds.CenterPoint().y(),
              readout()->bounds().CenterPoint().y(), 1);
  EXPECT_EQ(AvoraResizeReadoutView::BreakpointLabel(page()->width()),
            readout()->GetBreakpointTextForTesting());

  // Never takes the mouse from the page.
  EXPECT_FALSE(readout()->GetCanProcessEventsWithinSubtree());

  // Lingers, then fades once the resize ends.
  contents_area()->SetAvoraLiveResizing(false);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return readout()->layer()->GetTargetOpacity() == 0.f; }));

  // Later resizes leave it alone.
  const std::u16string last = readout()->GetSizeTextForTesting();
  ResizeWindow(1200, 900);
  EXPECT_EQ(last, readout()->GetSizeTextForTesting());
}

}  // namespace

}  // namespace avora
