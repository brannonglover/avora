// Copyright 2026 Avora. All rights reserved.

#include <string>

#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/devtools/devtools_window_testing.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_page_edge_handle.h"
#include "chrome/browser/ui/views/avora/avora_resize_readout_view.h"
#include "chrome/browser/ui/views/avora/avora_side_by_side.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/hit_test.h"
#include "ui/base/page_transition_types.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace avora {

namespace {

constexpr char kPage[] = "data:text/html,<title>t</title><h1>page</h1>";

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

// ── Narrowing the page by its edges ─────────────────────────────────────────

class AvoraPageEdgeBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    browser_view()->GetWidget()->SetBounds(gfx::Rect(0, 0, 1400, 900));
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(kPage)));
    Layout();
  }

 protected:
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  MultiContentsView* contents_area() {
    return browser_view()->multi_contents_view();
  }
  ContentsContainerView* container() {
    return contents_area()->GetPrimaryContentsContainerView();
  }
  ContentsWebView* page() { return container()->contents_view(); }
  AvoraResizeReadoutView* readout() {
    return contents_area()->avora_resize_readout_for_testing();
  }
  void Layout() { views::test::RunScheduledLayout(browser_view()); }

  AvoraPageEdgeHandle* handle(AvoraPageEdgeHandle::Edge edge) {
    for (views::View* child : browser_view()->children()) {
      auto* handle = views::AsViewClass<AvoraPageEdgeHandle>(child);
      if (handle && handle->edge() == edge) {
        return handle;
      }
    }
    return nullptr;
  }
  AvoraPageEdgeHandle* left() {
    return handle(AvoraPageEdgeHandle::Edge::kLeft);
  }
  AvoraPageEdgeHandle* right() {
    return handle(AvoraPageEdgeHandle::Edge::kRight);
  }

  // What macOS asks the window first, before any view sees the press: a
  // caption here would move the window instead of reaching the handle.
  int HitTestAt(views::View* view) {
    gfx::Point point = view->GetLocalBounds().CenterPoint();
    views::View::ConvertPointToWidget(view, &point);
    return browser_view()->GetWidget()->GetNonClientComponent(point);
  }

  gfx::Rect InBrowserView(views::View* view) {
    return views::View::ConvertRectToTarget(view, browser_view(),
                                            view->GetLocalBounds());
  }

  ui::MouseEvent Mouse(ui::EventType type,
                       views::View* view,
                       int dx,
                       int flags = 0) {
    gfx::Point local = view->GetLocalBounds().CenterPoint();
    gfx::Point root = local;
    views::View::ConvertPointToWidget(view, &root);
    local.Offset(dx, 0);
    root.Offset(dx, 0);
    return ui::MouseEvent(type, local, root, base::TimeTicks::Now(),
                          ui::EF_LEFT_MOUSE_BUTTON | flags,
                          ui::EF_LEFT_MOUSE_BUTTON);
  }

  // Presses |view|, drags it |dx| and, unless |release| is false, lets go.
  void Drag(views::View* view, int dx, bool release = true) {
    // Synthetic presses at the same spot within a view count as a double
    // click; each drag here is a fresh single press.
    ui::MouseEvent::ResetLastClickForTest();
    ASSERT_TRUE(view->OnMousePressed(
        Mouse(ui::EventType::kMousePressed, view, 0)));
    ASSERT_TRUE(view->OnMouseDragged(
        Mouse(ui::EventType::kMouseDragged, view, dx)));
    Layout();
    if (release) {
      view->OnMouseReleased(Mouse(ui::EventType::kMouseReleased, view, dx));
      Layout();
    }
  }

  int InnerWidth(int expected) {
    return content::EvalJs(
               browser()->tab_strip_model()->GetActiveWebContents(),
               base::StringPrintf(
                   "new Promise(resolve => {"
                   "  const deadline = performance.now() + 5000;"
                   "  const check = () => {"
                   "    if (innerWidth === %d || performance.now() > deadline)"
                   "      resolve(innerWidth);"
                   "    else"
                   "      requestAnimationFrame(check);"
                   "  };"
                   "  check();"
                   "})",
                   expected))
        .ExtractInt();
  }
};

IN_PROC_BROWSER_TEST_F(AvoraPageEdgeBrowserTest,
                       RightEdgeIsGrabbableBesideTheFullWidthPage) {
  ASSERT_TRUE(right());
  ASSERT_TRUE(left());
  EXPECT_TRUE(right()->GetVisible());
  // The left edge sits beside the sidebar's own resize strip, so it waits
  // until there is black to sit in.
  EXPECT_FALSE(left()->GetVisible());

  // Outside the page, so the page and its scrollbar keep every click, and
  // inside the window short of its edge.
  const gfx::Rect page_bounds = InBrowserView(page());
  EXPECT_FALSE(right()->bounds().Intersects(page_bounds));
  EXPECT_EQ(page_bounds.right(), right()->x());
  EXPECT_LT(right()->bounds().right(), browser_view()->width());

  // Nothing shows there until the page is resized.
  EXPECT_FALSE(right()->IsGripShown());

  // A press there reaches the handle rather than dragging the window.
  EXPECT_EQ(HTCLIENT, HitTestAt(right()));

  // It is what the mouse finds there.
  EXPECT_EQ(right(), browser_view()->GetWidget()->GetRootView()
                         ->GetEventHandlerForPoint(views::View::ConvertPointToTarget(
                             right(), browser_view()->GetWidget()->GetRootView(),
                             right()->GetLocalBounds().CenterPoint())));
}

IN_PROC_BROWSER_TEST_F(AvoraPageEdgeBrowserTest,
                       DraggingNarrowsThePageInPlaceWithBlackEitherSide) {
  const gfx::Rect window_before = browser_view()->GetWidget()->GetWindowBoundsInScreen();
  const gfx::Rect area_before = contents_area()->bounds();
  const int full_width = page()->width();
  const int centre = InBrowserView(page()).CenterPoint().x();

  Drag(right(), -300);

  // Both edges moved in: twice the drag, centred where it was.
  EXPECT_EQ(full_width - 600, page()->width());
  EXPECT_NEAR(centre, InBrowserView(page()).CenterPoint().x(), 1);
  EXPECT_EQ(full_width - 600, InnerWidth(full_width - 600));

  // The window and the contents area stay as they were.
  EXPECT_EQ(window_before, browser_view()->GetWidget()->GetWindowBoundsInScreen());
  EXPECT_EQ(area_before, contents_area()->bounds());

  // Black fills either side, and both edges now have a handle in it.
  views::View* backdrop = nullptr;
  for (views::View* child : contents_area()->children()) {
    // The black: visible, wider than the page and around it.
    if (child->GetVisible() && child != readout() &&
        !views::IsViewClass<ContentsContainerView>(child) &&
        child->width() > container()->width() &&
        child->bounds().Contains(container()->bounds())) {
      backdrop = child;
    }
  }
  ASSERT_TRUE(backdrop);
  // The grips stay while the page is narrowed ...
  EXPECT_TRUE(left()->IsGripShown());
  EXPECT_TRUE(right()->IsGripShown());
  EXPECT_TRUE(left()->GetVisible())
      << "left " << left()->bounds().ToString() << " page "
      << InBrowserView(page()).ToString() << " area "
      << InBrowserView(contents_area()).ToString() << " container "
      << container()->bounds().ToString();
  EXPECT_TRUE(right()->GetVisible());
  EXPECT_EQ(InBrowserView(page()).x(), left()->bounds().right());
  EXPECT_EQ(InBrowserView(page()).right(), right()->x());
  EXPECT_EQ(HTCLIENT, HitTestAt(left()));
  EXPECT_EQ(HTCLIENT, HitTestAt(right()));

  // The left handle works the same way, from the other side.
  const int narrowed = page()->width();
  Drag(left(), 50);  // Inward on the left is to the right.
  EXPECT_EQ(narrowed - 100, page()->width());
  EXPECT_NEAR(centre, InBrowserView(page()).CenterPoint().x(), 1);

  // Pulled all the way out, it fills its space again.
  Drag(right(), 2000);
  EXPECT_EQ(full_width, page()->width());
  EXPECT_FALSE(contents_area()->avora_page_width());
  EXPECT_FALSE(backdrop->GetVisible());
  EXPECT_FALSE(left()->GetVisible());
  // ... and go once it is back to full width, though the right edge can still
  // be grabbed.
  EXPECT_FALSE(right()->IsGripShown());
  EXPECT_TRUE(right()->GetVisible());
  EXPECT_EQ(HTCLIENT, HitTestAt(right()));
}

IN_PROC_BROWSER_TEST_F(AvoraPageEdgeBrowserTest,
                       ReadoutShowsThePageSizeWhileDragging) {
  EXPECT_FALSE(readout()->GetVisible());

  EXPECT_FALSE(right()->IsGripShown());
  Drag(right(), -250, /*release=*/false);
  EXPECT_TRUE(right()->IsGripShown());
  EXPECT_TRUE(readout()->GetVisible());
  EXPECT_EQ(base::NumberToString16(page()->width()) + u" \u00D7 " +
                base::NumberToString16(page()->height()),
            readout()->GetSizeTextForTesting());
  EXPECT_EQ(AvoraResizeReadoutView::BreakpointLabel(page()->width()),
            readout()->GetBreakpointTextForTesting());
  // Over the middle of the page, never taking the mouse from it.
  EXPECT_NEAR(InBrowserView(page()).CenterPoint().x(),
              InBrowserView(readout()).CenterPoint().x(), 1);
  EXPECT_FALSE(readout()->GetCanProcessEventsWithinSubtree());

  right()->OnMouseReleased(
      Mouse(ui::EventType::kMouseReleased, right(), -250));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return readout()->layer()->GetTargetOpacity() == 0.f; }));
}

IN_PROC_BROWSER_TEST_F(AvoraPageEdgeBrowserTest, DoubleClickFillsAgain) {
  const int full_width = page()->width();
  Drag(right(), -300);
  ASSERT_LT(page()->width(), full_width);

  ASSERT_TRUE(right()->OnMousePressed(
      Mouse(ui::EventType::kMousePressed, right(), 0, ui::EF_IS_DOUBLE_CLICK)));
  Layout();
  EXPECT_EQ(full_width, page()->width());
}

IN_PROC_BROWSER_TEST_F(AvoraPageEdgeBrowserTest, WidthHoldsAcrossTabs) {
  Drag(right(), -300);
  const int narrowed = page()->width();

  ASSERT_TRUE(AddTabAtIndex(1, GURL(kPage), ui::PAGE_TRANSITION_TYPED));
  Layout();
  EXPECT_EQ(narrowed, page()->width());

  browser()->tab_strip_model()->ActivateTabAt(0);
  Layout();
  EXPECT_EQ(narrowed, page()->width());
}

IN_PROC_BROWSER_TEST_F(AvoraPageEdgeBrowserTest,
                       DockedDevToolsTurnsResizingOff) {
  const int full_width = page()->width();
  Drag(right(), -300);
  const int narrowed = page()->width();
  ASSERT_LT(narrowed, full_width);

  DevToolsWindow* const devtools =
      DevToolsWindowTesting::OpenDevToolsWindowSync(browser(),
                                                    /*is_docked=*/true);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return container()->devtools_web_view()->GetVisible(); }));
  Layout();
  // The page and DevTools share the full space, with nothing to grab.
  EXPECT_EQ(contents_area()->GetLocalBounds().width(), container()->width());
  EXPECT_FALSE(left()->GetVisible());
  EXPECT_FALSE(right()->GetVisible());

  // Closed again, the page goes back to the width it had.
  DevToolsWindowTesting::CloseDevToolsWindowSync(devtools);
  Layout();
  EXPECT_EQ(narrowed, page()->width());
  EXPECT_TRUE(right()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(AvoraPageEdgeBrowserTest, SplitViewFillsAsUsual) {
  Drag(right(), -300);
  ASSERT_TRUE(AddTabAtIndex(1, GURL(kPage), ui::PAGE_TRANSITION_TYPED));
  TabStripModel* const model = browser()->tab_strip_model();
  model->ActivateTabAt(0);
  PutTabsSideBySide(*model, model->GetTabAtIndex(0), model->GetTabAtIndex(1),
                    split_tabs::SplitTabCreatedSource::kTabContextMenu);
  Layout();
  ASSERT_TRUE(contents_area()->IsInSplitView());
  EXPECT_FALSE(left()->GetVisible());
  EXPECT_FALSE(right()->GetVisible());
}

}  // namespace

}  // namespace avora
