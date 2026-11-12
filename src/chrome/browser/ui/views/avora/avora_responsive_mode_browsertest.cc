// Copyright 2026 Avora. All rights reserved.

#include <string>

#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/devtools/devtools_window_testing.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_responsive_frame_view.h"
#include "chrome/browser/ui/views/avora/avora_side_by_side.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/base/page_transition_types.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace avora {

namespace {

constexpr char kPage[] =
    "data:text/html,<!doctype html><title>responsive</title><h1>page</h1>";

class AvoraResponsiveModeBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    // Room for a phone-width page and its gutters, with plenty to spare.
    browser_view()->GetWidget()->SetBounds(gfx::Rect(0, 0, 1400, 1000));
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(kPage)));
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
  content::WebContents* active_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  AvoraResponsiveFrameView* frame() {
    for (views::View* child : contents_area()->children()) {
      if (auto* frame = views::AsViewClass<AvoraResponsiveFrameView>(child)) {
        return frame;
      }
    }
    return nullptr;
  }

  void Toggle() {
    chrome::ExecuteCommand(browser(), IDC_AVORA_TOGGLE_RESPONSIVE);
    views::test::RunScheduledLayout(browser_view());
  }

  // The page's own idea of its width, once it has caught up with a resize.
  int InnerWidth(int expected) {
    return content::EvalJs(
               active_contents(),
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

  // The page's bounds in the contents area's coordinates.
  gfx::Rect PageBoundsInContentsArea() {
    return views::View::ConvertRectToTarget(page(), contents_area(),
                                            page()->GetLocalBounds());
  }

  // Drags |handle| by |dx| the way the mouse does.
  void Drag(views::View* handle, int dx, int flags = 0) {
    const gfx::Point start = handle->GetLocalBounds().CenterPoint();
    gfx::Point start_root = start;
    views::View::ConvertPointToWidget(handle, &start_root);
    const gfx::Point end = start + gfx::Vector2d(dx, 0);
    const gfx::Point end_root = start_root + gfx::Vector2d(dx, 0);
    const base::TimeTicks now = base::TimeTicks::Now();

    ui::MouseEvent press(ui::EventType::kMousePressed, start, start_root, now,
                         ui::EF_LEFT_MOUSE_BUTTON | flags,
                         ui::EF_LEFT_MOUSE_BUTTON);
    ui::MouseEvent drag(ui::EventType::kMouseDragged, end, end_root, now,
                        ui::EF_LEFT_MOUSE_BUTTON | flags,
                        ui::EF_LEFT_MOUSE_BUTTON);
    ui::MouseEvent release(ui::EventType::kMouseReleased, end, end_root, now,
                           ui::EF_LEFT_MOUSE_BUTTON | flags,
                           ui::EF_LEFT_MOUSE_BUTTON);
    ASSERT_TRUE(handle->OnMousePressed(press));
    ASSERT_TRUE(handle->OnMouseDragged(drag));
    handle->OnMouseReleased(release);
    views::test::RunScheduledLayout(browser_view());
  }

  // The frame's children: the toolbar, then the left, right and bottom
  // handles.
  views::View* right_handle() { return frame()->children()[2]; }
};

IN_PROC_BROWSER_TEST_F(AvoraResponsiveModeBrowserTest,
                       ShrinksThePageToPhoneWidthAndBack) {
  const int full_width = page()->width();
  ASSERT_GT(full_width, 600);
  EXPECT_FALSE(contents_area()->IsAvoraResponsiveModeActive());

  Toggle();
  ASSERT_TRUE(contents_area()->IsAvoraResponsiveModeActive());
  ASSERT_TRUE(frame());
  EXPECT_TRUE(frame()->GetVisible());

  // The page really is laid out at the phone's width: media queries see it.
  EXPECT_EQ(390, page()->width());
  EXPECT_EQ(390, InnerWidth(390));

  // Centred in the contents area.
  const gfx::Rect bounds = PageBoundsInContentsArea();
  EXPECT_NEAR(bounds.CenterPoint().x(),
              contents_area()->GetLocalBounds().CenterPoint().x(), 1);

  Toggle();
  EXPECT_FALSE(contents_area()->IsAvoraResponsiveModeActive());
  EXPECT_FALSE(frame()->GetVisible());
  EXPECT_EQ(full_width, page()->width());
  EXPECT_EQ(full_width, InnerWidth(full_width));
}

IN_PROC_BROWSER_TEST_F(AvoraResponsiveModeBrowserTest, BelongsToTheTab) {
  content::WebContents* const first = active_contents();
  Toggle();
  ASSERT_EQ(390, page()->width());

  ASSERT_TRUE(AddTabAtIndex(1, GURL(kPage), ui::PAGE_TRANSITION_TYPED));
  views::test::RunScheduledLayout(browser_view());
  EXPECT_NE(first, active_contents());
  EXPECT_FALSE(contents_area()->IsAvoraResponsiveModeActive());
  EXPECT_GT(page()->width(), 600);

  browser()->tab_strip_model()->ActivateTabAt(0);
  views::test::RunScheduledLayout(browser_view());
  EXPECT_EQ(first, active_contents());
  EXPECT_TRUE(contents_area()->IsAvoraResponsiveModeActive());
  EXPECT_EQ(390, page()->width());
}

IN_PROC_BROWSER_TEST_F(AvoraResponsiveModeBrowserTest,
                       DraggingAnEdgeResizesSymmetricallyAndSnaps) {
  Toggle();
  ASSERT_EQ(390, page()->width());
  const int centre = PageBoundsInContentsArea().CenterPoint().x();

  // A side handle moves both sides, so the width grows by twice the drag and
  // the page stays where it was.
  Drag(right_handle(), 50);
  EXPECT_EQ(490, page()->width());
  EXPECT_NEAR(centre, PageBoundsInContentsArea().CenterPoint().x(), 1);

  // Near a breakpoint the edge snaps onto it ...
  Drag(right_handle(), 73);  // 490 + 146 = 636, within 8 of 640.
  EXPECT_EQ(640, page()->width());

  // ... unless Option is held.
  Drag(right_handle(), -3, ui::EF_ALT_DOWN);  // 640 - 6 = 634.
  EXPECT_EQ(634, page()->width());
  EXPECT_EQ(634, InnerWidth(634));
}

IN_PROC_BROWSER_TEST_F(AvoraResponsiveModeBrowserTest,
                       DockedDevToolsStayBesideTheFrame) {
  Toggle();
  ASSERT_EQ(390, page()->width());

  DevToolsWindow* const devtools =
      DevToolsWindowTesting::OpenDevToolsWindowSync(browser(),
                                                    /*is_docked=*/true);
  views::test::RunScheduledLayout(browser_view());

  // DevTools keeps its own place and the page keeps its width.  The docked
  // DevTools view spans the whole container with the page laid over the part
  // DevTools leaves (its "inspected page" bounds), so that part is where the
  // frame must stay.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return container()->devtools_web_view()->GetVisible() &&
           !container()->contents_resizing_strategy().bounds().IsEmpty();
  }));
  views::test::RunScheduledLayout(browser_view());
  EXPECT_EQ(390, page()->width());

  gfx::Rect inspected = container()->contents_resizing_strategy().bounds();
  inspected.Offset(container()->bounds().OffsetFromOrigin());
  ASSERT_LT(inspected.height(), container()->height());
  EXPECT_TRUE(inspected.Contains(frame()->bounds()))
      << "inspected " << inspected.ToString() << " frame "
      << frame()->bounds().ToString();
  EXPECT_TRUE(inspected.Contains(PageBoundsInContentsArea()));

  // The handles still take the mouse rather than the DevTools view below
  // them: on macOS this lookup is what decides whether a click goes to the
  // views hierarchy or to the native view underneath.
  views::View* const handle = right_handle();
  const gfx::Point centre = views::View::ConvertPointToTarget(
      handle, browser_view()->GetWidget()->GetRootView(),
      handle->GetLocalBounds().CenterPoint());
  EXPECT_EQ(handle,
            browser_view()->GetWidget()->GetRootView()->GetEventHandlerForPoint(
                centre));
  Drag(handle, 50);
  EXPECT_EQ(490, page()->width());

  DevToolsWindowTesting::CloseDevToolsWindowSync(devtools);
}

IN_PROC_BROWSER_TEST_F(AvoraResponsiveModeBrowserTest, SplitViewSetsItAside) {
  Toggle();
  ASSERT_TRUE(contents_area()->IsAvoraResponsiveModeActive());

  ASSERT_TRUE(AddTabAtIndex(1, GURL(kPage), ui::PAGE_TRANSITION_TYPED));
  TabStripModel* const model = browser()->tab_strip_model();
  model->ActivateTabAt(0);
  PutTabsSideBySide(*model, model->GetTabAtIndex(0), model->GetTabAtIndex(1),
                    split_tabs::SplitTabCreatedSource::kTabContextMenu);
  views::test::RunScheduledLayout(browser_view());
  ASSERT_TRUE(contents_area()->IsInSplitView());
  EXPECT_FALSE(contents_area()->IsAvoraResponsiveModeActive());
  EXPECT_FALSE(frame()->GetVisible());
}

}  // namespace

}  // namespace avora
