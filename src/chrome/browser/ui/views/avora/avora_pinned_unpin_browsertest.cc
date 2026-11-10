// Copyright 2026 Avora. All rights reserved.

#include <string>

#include "base/strings/string_number_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/avora/avora_pinned_items.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_materializer.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_tab_marker.h"
#include "chrome/browser/ui/views/avora/avora_pinned_section_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/avora_side_nav_scroll.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/window_open_disposition.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace avora {

namespace {

template <typename T>
T* FindView(views::View* root,
            const base::RepeatingCallback<bool(T*)>& match) {
  if (auto* view = views::AsViewClass<T>(root); view && match.Run(view)) {
    return view;
  }
  for (views::View* child : root->children()) {
    if (T* found = FindView<T>(child, match)) {
      return found;
    }
  }
  return nullptr;
}

// The row's trailing hover button: the only Button a FolderTabRow holds, and
// the last one a PinnedTabRow holds (its favicon slot comes first).
views::Button* TrailingButton(views::View* row) {
  views::Button* last = nullptr;
  for (views::View* child : row->children()) {
    if (auto* button = views::AsViewClass<views::Button>(child)) {
      last = button;
    }
  }
  return last;
}

void Click(views::Button* button) {
  views::test::ButtonTestApi(button).NotifyClick(ui::MouseEvent(
      ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
      ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON, 0));
}

}  // namespace

// A pinned tab whose live tab has been closed still has a row, and that row
// has to offer a way to get rid of the pin: the "x" where an open pin shows
// its minus.
class AvoraPinnedUnpinBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  GURL TestURL(const std::string& host) {
    return embedded_test_server()->GetURL(host, "/title1.html");
  }

  static views::View* Root(BrowserWindowInterface* b) {
    return BrowserView::GetBrowserViewForBrowser(b);
  }

  static AvoraPinnedSectionView* Section(BrowserWindowInterface* b) {
    return FindView<AvoraPinnedSectionView>(
        Root(b), base::BindRepeating([](AvoraPinnedSectionView*) {
          return true;
        }));
  }

  static PinnedTabRow* LiveRow(BrowserWindowInterface* b, content::WebContents* contents) {
    return FindView<PinnedTabRow>(
        Root(b), base::BindRepeating(
                     [](content::WebContents* wc, PinnedTabRow* row) {
                       return row->web_contents() == wc;
                     },
                     contents));
  }

  static FolderTabRow* ClosedRow(BrowserWindowInterface* b, const GURL& url) {
    return FindView<FolderTabRow>(
        Root(b), base::BindRepeating(
                     [](const std::string& spec, FolderTabRow* row) {
                       return row->url() == spec;
                     },
                     url.spec()));
  }

  // Opens |url| in |b|'s active tab and pins it with a kPinned record, as
  // the pin gesture does.  Returns the item id.
  std::string PinActiveTab(BrowserWindowInterface* b, const GURL& url) {
    EXPECT_TRUE(ui_test_utils::NavigateToURL(b, url));
    TabStripModel* model = b->GetTabStripModel();
    content::WebContents* contents = model->GetActiveWebContents();
    AvoraPinnedSectionView* section = Section(b);
    EXPECT_TRUE(section);
    PinAndCreatePinnedItem(model, section->GetPinnedItemsManager(), contents);
    return GetPinnedItemIdForTab(contents);
  }
};

IN_PROC_BROWSER_TEST_F(AvoraPinnedUnpinBrowserTest, XRemovesClosedPin) {
  TabStripModel* model = browser()->GetTabStripModel();
  const GURL pin_url = TestURL("a.test");
  const std::string item_id = PinActiveTab(browser(), pin_url);
  ASSERT_FALSE(item_id.empty());
  content::WebContents* pinned = model->GetActiveWebContents();

  // A daily tab to land on once the pin's tab is closed.
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), TestURL("b.test"), WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  ASSERT_EQ(2, model->count());

  // The minus closes the live tab and keeps the pin.
  ASSERT_TRUE(base::test::RunUntil([&]() { return LiveRow(browser(), pinned) != nullptr; }));
  views::Button* minus = TrailingButton(LiveRow(browser(), pinned));
  ASSERT_TRUE(minus);
  EXPECT_EQ(u"Close Tab", minus->GetTooltipText());
  Click(minus);

  ASSERT_TRUE(base::test::RunUntil([&]() { return model->count() == 1; }));
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return ClosedRow(browser(), pin_url) != nullptr; }));
  PinnedItemsManager* items = Section(browser())->GetPinnedItemsManager();
  ASSERT_TRUE(items->GetPinnedItemById(item_id));

  // The closed row's "x": hidden until hovered, and it unpins.
  views::Button* x = TrailingButton(ClosedRow(browser(), pin_url));
  ASSERT_TRUE(x);
  EXPECT_FALSE(x->GetVisible());
  EXPECT_EQ(u"Unpin", x->GetTooltipText());
  Click(x);

  ASSERT_TRUE(
      base::test::RunUntil([&]() { return !ClosedRow(browser(), pin_url); }));
  EXPECT_FALSE(Section(browser())->GetPinnedItemsManager()->GetPinnedItemById(
      item_id));
  // Nothing else is touched: the daily tab is still there.
  EXPECT_EQ(1, model->count());
}

// Materialization is per window, so a pin closed here may still be open in
// another window.  Unpinning it here must release that tab to its window's
// daily list -- open, unpinned and unmarked -- not leave it marked with an
// item that no longer exists, which hides it from both lists.
IN_PROC_BROWSER_TEST_F(AvoraPinnedUnpinBrowserTest,
                       XReleasesPinOpenInAnotherWindow) {
  const GURL pin_url = TestURL("a.test");
  const std::string item_id = PinActiveTab(browser(), pin_url);
  ASSERT_FALSE(item_id.empty());
  TabStripModel* model = browser()->GetTabStripModel();
  content::WebContents* live = model->GetActiveWebContents();
  ASSERT_TRUE(model->IsTabPinned(model->GetIndexOfWebContents(live)));

  // A second window in the same Space shows the pin with no live tab.
  BrowserWindowInterface* other = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&]() { return ClosedRow(other, pin_url) != nullptr; }));

  Click(TrailingButton(ClosedRow(other, pin_url)));

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !ClosedRow(other, pin_url) && !LiveRow(browser(), live);
  }));
  EXPECT_FALSE(
      Section(other)->GetPinnedItemsManager()->GetPinnedItemById(item_id));
  // The first window's tab is still open, as an ordinary tab.
  const int index = model->GetIndexOfWebContents(live);
  ASSERT_NE(TabStripModel::kNoTab, index);
  EXPECT_FALSE(model->IsTabPinned(index));
  EXPECT_TRUE(GetPinnedItemIdForTab(live).empty());
}

// Opening a closed pin must leave the side nav where it is.  Its tab used to
// be marked only after it had been inserted and activated, so for that moment
// it was an ordinary daily tab, and the tab strip scrolled the nav to its new
// row at the end of the daily list just before the row was hidden.
IN_PROC_BROWSER_TEST_F(AvoraPinnedUnpinBrowserTest,
                       OpeningClosedPinKeepsSideNavScroll) {
  TabStripModel* model = browser()->GetTabStripModel();
  const GURL pin_url = TestURL("a.test");
  ASSERT_FALSE(PinActiveTab(browser(), pin_url).empty());
  content::WebContents* pinned = model->GetActiveWebContents();

  // Enough daily tabs that the nav overflows, so there is somewhere to jump.
  for (int i = 0; i < 40; ++i) {
    ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), TestURL("d" + base::NumberToString(i) + ".test"),
        WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  }
  model->CloseWebContentsAt(model->GetIndexOfWebContents(pinned),
                            TabCloseTypes::CLOSE_NONE);

  views::Widget* widget = Root(browser())->GetWidget();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    widget->LayoutRootViewIfNecessary();
    FolderTabRow* row = ClosedRow(browser(), pin_url);
    return row && row->width() > 0;
  }));
  views::ScrollView* scroll_view =
      FindEnclosingScrollView(ClosedRow(browser(), pin_url));
  ASSERT_TRUE(scroll_view);
  auto* strip = views::AsViewClass<TabStripView>(scroll_view->parent());
  ASSERT_TRUE(strip);
  // Let the strip finish bringing the last daily tab into view, then put the
  // pin back on screen.
  ASSERT_TRUE(base::test::RunUntil([&]() {
    widget->LayoutRootViewIfNecessary();
    return !strip->GetPrimaryScrollTargetForTesting();
  }));
  ASSERT_TRUE(scroll_view->IsVerticalContentOverflowing());
  scroll_view->ScrollToOffset(gfx::PointF());
  widget->LayoutRootViewIfNecessary();
  ASSERT_EQ(0, scroll_view->GetVisibleRect().y());

  // A real click, through the widget: the row only acts on release.
  FolderTabRow* row = ClosedRow(browser(), pin_url);
  gfx::Point point = row->GetLocalBounds().CenterPoint();
  views::View::ConvertPointToWidget(row, &point);
  ui::MouseEvent press(ui::EventType::kMousePressed, point, point,
                       ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                       ui::EF_LEFT_MOUSE_BUTTON);
  widget->OnMouseEvent(&press);
  ui::MouseEvent release(ui::EventType::kMouseReleased, point, point,
                         ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                         ui::EF_LEFT_MOUSE_BUTTON);
  widget->OnMouseEvent(&release);

  content::WebContents* opened = model->GetActiveWebContents();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    widget->LayoutRootViewIfNecessary();
    return LiveRow(browser(), opened) != nullptr;
  }));
  EXPECT_FALSE(GetPinnedItemIdForTab(opened).empty());
  EXPECT_EQ(pin_url, opened->GetVisibleURL());
  EXPECT_EQ(0, scroll_view->GetVisibleRect().y());
}

}  // namespace avora
