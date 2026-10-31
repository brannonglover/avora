// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_side_by_side.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_tab_space.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_change_type.h"
#include "chrome/browser/ui/tabs/tab_menu_model.h"
#include "chrome/browser/ui/tabs/tab_menu_model_delegate.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_link_preview_tab_state.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/tabs/common/split_tab_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/common/unpinned_tab_container_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/base/models/menu_model.h"
#include "ui/base/test/ui_controls.h"
#include "ui/base/window_open_disposition.h"
#include "ui/compositor/layer.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace avora {

namespace {

using SecondaryContentsMode = MultiContentsView::SecondaryContentsMode;

class NoOpMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  void ExecuteCommand(int command_id, int event_flags) override {}
};

std::optional<size_t> FindMenuItem(const ui::MenuModel& menu,
                                   std::u16string_view label) {
  for (size_t i = 0; i < menu.GetItemCount(); ++i) {
    if (menu.GetLabelAt(i) == label) {
      return i;
    }
  }
  return std::nullopt;
}

}  // namespace

class AvoraSideBySideBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        &AvoraSideBySideBrowserTest::HandleRequest, base::Unretained(this)));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  std::unique_ptr<net::test_server::HttpResponse> HandleRequest(
      const net::test_server::HttpRequest& request) {
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    response->set_content_type("text/html");
    const std::string host(request.GetURL().host());
    response->set_content("<!doctype html><title>" + host +
                          "</title><h1>" + host + "</h1>"
                          "<a id='go' href='/target.html' target='_blank'>open"
                          "</a>");
    return response;
  }

  GURL TestURL(const std::string& host) {
    return embedded_test_server()->GetURL(host, "/page.html");
  }

  TabStripModel* model() { return browser()->tab_strip_model(); }
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  MultiContentsView* multi_contents_view() {
    return browser_view()->multi_contents_view();
  }
  std::string ActiveSpace() {
    return GetWindowSpaceState(browser())->active_space_id();
  }

  // Three daily tabs a, b and c, in that order, with a active.
  void OpenThreeTabs() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), TestURL("a.test")));
    a_ = model()->GetActiveTab();
    b_ = OpenBackgroundTab(TestURL("b.test"));
    c_ = OpenBackgroundTab(TestURL("c.test"));
    ASSERT_EQ(3, model()->count());
    // Lets the Space filter tag and lay out what was just opened.
    ASSERT_TRUE(base::test::RunUntil([&] {
      return CanPutTabsSideBySide(a_, b_) && CanPutTabsSideBySide(b_, c_);
    }));
  }

  tabs::TabInterface* OpenBackgroundTab(const GURL& url) {
    ui_test_utils::NavigateToURLWithDisposition(
        browser(), url, WindowOpenDisposition::NEW_BACKGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
    return model()->GetTabAtIndex(model()->count() - 1);
  }

  void Pair(tabs::TabInterface* anchor, tabs::TabInterface* incoming) {
    PutTabsSideBySide(*model(), anchor, incoming,
                      split_tabs::SplitTabCreatedSource::kTabContextMenu);
    ASSERT_TRUE(anchor->IsSplit());
  }

  bool ArePair(tabs::TabInterface* left, tabs::TabInterface* right) {
    if (!left->IsSplit() || left->GetSplit() != right->GetSplit()) {
      return false;
    }
    return model()->GetSplitData(*left->GetSplit())->ListTabs() ==
           std::vector<tabs::TabInterface*>{left, right};
  }

  std::unique_ptr<TabMenuModel> MenuFor(tabs::TabInterface* tab) {
    return std::make_unique<TabMenuModel>(
        &menu_delegate_, TabMenuModelDelegate::From(browser()), model(),
        model()->GetIndexOfTab(tab));
  }

  TabStripView* strip() {
    return views::AsViewClass<TabStripView>(
        browser_view()->tab_strip_view()->GetTabStripView());
  }

  // The row drawn for |tab|: its own, or its half of a pair's.
  TabView* RowFor(const tabs::TabInterface* tab) {
    for (views::View* child : strip()->GetUnpinnedTabsContainer()->children()) {
      if (auto* tab_view = views::AsViewClass<TabView>(child)) {
        if (tab_view->GetTabInterface() == tab) {
          return tab_view;
        }
      }
      if (auto* split_view = views::AsViewClass<SplitTabView>(child)) {
        for (views::View* half : split_view->children()) {
          auto* tab_view = views::AsViewClass<TabView>(half);
          if (tab_view && tab_view->GetTabInterface() == tab) {
            return tab_view;
          }
        }
      }
    }
    return nullptr;
  }

  SplitTabView* SplitRowFor(const tabs::TabInterface* tab) {
    TabView* row = RowFor(tab);
    return row ? views::AsViewClass<SplitTabView>(row->parent()) : nullptr;
  }

  gfx::Point CenterOf(const tabs::TabInterface* tab) {
    TabView* row = RowFor(tab);
    EXPECT_TRUE(row);
    return row ? row->GetBoundsInScreen().CenterPoint() : gfx::Point();
  }

  // ── Real input.  Drags go through ui_controls rather than calling into
  // TabDragHandler, because what can go wrong is who holds the mouse. ──

  void PrepareForInput() {
    ui_controls::EnableUIControls();
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    ASSERT_TRUE(base::test::RunUntil([&] {
      views::View* container = strip()->GetUnpinnedTabsContainer();
      return !container->needs_layout() && RowFor(a_) && RowFor(b_) &&
             RowFor(c_);
    }));
  }

  void MoveMouseTo(const gfx::Point& point) {
    ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(point));
  }

  // Glides to wherever |target| says, in steps, the way a hand does: with a
  // frame between moves, so a reorder has laid out the list before the next
  // move is measured against it, and aiming afresh at each one.  Favorites and
  // pinned open their drop zones during a drag, which moves the daily list by
  // tens of DIPs as the pointer enters and leaves it, so a row has to be
  // followed rather than measured once.
  template <typename Target>
  void GlideMouse(Target target) {
    static constexpr int kSteps = 8;
    for (int remaining = kSteps; remaining > 0; --remaining) {
      const gfx::Vector2d to_go = target() - mouse_location_;
      mouse_location_ += gfx::Vector2d(to_go.x() / remaining,
                                       to_go.y() / remaining);
      MoveMouseTo(mouse_location_);
      Wait(base::Milliseconds(30));
    }
    mouse_location_ = target();
    MoveMouseTo(mouse_location_);
    Wait(base::Milliseconds(30));
  }

  void GlideMouseBy(const gfx::Vector2d& by) {
    const gfx::Point to = mouse_location_ + by;
    GlideMouse([&] { return to; });
  }

  void GlideMouseToCenterOf(const tabs::TabInterface* tab) {
    GlideMouse([&] { return CenterOf(tab); });
  }

  void GlideMouseNearBottomOf(const tabs::TabInterface* tab) {
    GlideMouse([&] {
      return RowFor(tab)->GetBoundsInScreen().bottom_center() +
             gfx::Vector2d(0, -2);
    });
  }

  void Press(const gfx::Point& point) {
    MoveMouseTo(point);
    mouse_location_ = point;
    ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(ui_controls::LEFT,
                                                   ui_controls::DOWN));
  }

  // Picks up |tab| and carries it |by| from where it was.
  void StartDragging(const tabs::TabInterface* tab, const gfx::Vector2d& by) {
    Press(CenterOf(tab));
    GlideMouseBy(by);
    Wait(base::Milliseconds(400));
  }

  // Far enough to start a drag.
  static constexpr gfx::Vector2d kStartDrag{0, 14};

  void Release() {
    ASSERT_TRUE(
        ui_test_utils::SendMouseEventsSync(ui_controls::LEFT, ui_controls::UP));
    // A drop is carried out from a posted task.
    base::RunLoop run_loop;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, run_loop.QuitClosure());
    run_loop.Run();
  }

  void Wait(base::TimeDelta delay) {
    base::RunLoop run_loop;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), delay);
    run_loop.Run();
  }

  raw_ptr<tabs::TabInterface, DanglingUntriaged> a_ = nullptr;
  raw_ptr<tabs::TabInterface, DanglingUntriaged> b_ = nullptr;
  raw_ptr<tabs::TabInterface, DanglingUntriaged> c_ = nullptr;

 private:
  NoOpMenuDelegate menu_delegate_;
  gfx::Point mouse_location_;
};

// ── The pair itself ─────────────────────────────────────────────────────────

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest, ShowsBothTabsInTheWindow) {
  OpenThreeTabs();
  Pair(a_, b_);

  EXPECT_TRUE(ArePair(a_, b_));
  EXPECT_TRUE(multi_contents_view()->IsInSplitView());
  EXPECT_EQ(b_, model()->GetActiveTab());
  EXPECT_EQ(a_->GetContents(), multi_contents_view()
                                   ->contents_container_views()[0]
                                   ->contents_view()
                                   ->web_contents());
  EXPECT_EQ(b_->GetContents(), multi_contents_view()
                                   ->contents_container_views()[1]
                                   ->contents_view()
                                   ->web_contents());
}

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       PairsTabsNeitherOfWhichIsActive) {
  OpenThreeTabs();
  ASSERT_EQ(a_, model()->GetActiveTab());
  Pair(b_, c_);

  EXPECT_TRUE(ArePair(b_, c_));
  EXPECT_TRUE(multi_contents_view()->IsInSplitView());
}

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       TheTabBroughtOverAlwaysLandsOnTheRight) {
  OpenThreeTabs();
  Pair(c_, a_);
  EXPECT_TRUE(ArePair(c_, a_));
}

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest, PairsAreTwoTabsOnly) {
  OpenThreeTabs();
  Pair(a_, b_);
  EXPECT_FALSE(CanPutTabsSideBySide(a_, c_));
  EXPECT_FALSE(CanPutTabsSideBySide(c_, b_));
  EXPECT_FALSE(CanPutTabsSideBySide(c_, c_));
}

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest, LoadsAnUnloadedCopy) {
  OpenThreeTabs();
  const std::vector<BrowserWindowInterface*> before =
      GetAllBrowserWindowInterfaces();
  chrome::ExecuteCommand(browser(), IDC_NEW_WINDOW);
  BrowserWindowInterface* second = nullptr;
  for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
    if (!std::ranges::contains(before, window)) {
      second = window;
    }
  }
  ASSERT_TRUE(second);
  TabStripModel* second_model = second->GetTabStripModel();
  ASSERT_EQ(3, second_model->count());

  std::vector<tabs::TabInterface*> unloaded;
  for (int i = 0; i < second_model->count(); ++i) {
    tabs::TabInterface* tab = second_model->GetTabAtIndex(i);
    if (tab->GetContents()->GetController().NeedsReload()) {
      unloaded.push_back(tab);
    }
  }
  ASSERT_EQ(2u, unloaded.size());

  PutTabsSideBySide(*second_model, unloaded[0], unloaded[1],
                    split_tabs::SplitTabCreatedSource::kTabContextMenu);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return !unloaded[0]->GetContents()->GetController().NeedsReload() &&
           !unloaded[1]->GetContents()->GetController().NeedsReload();
  }));
}

// ── Right click ─────────────────────────────────────────────────────────────

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       MenuPairsWithAnyOtherDailyTab) {
  OpenThreeTabs();
  auto menu = MenuFor(c_);
  const std::optional<size_t> item =
      FindMenuItem(*menu, u"Open Side by Side With");
  ASSERT_TRUE(item.has_value());
  ui::MenuModel* partners = menu->GetSubmenuModelAt(*item);
  ASSERT_TRUE(partners);

  // The active tab first, so the common case is the top row.
  ASSERT_EQ(2u, partners->GetItemCount());
  EXPECT_EQ(a_->GetContents()->GetTitle(), partners->GetLabelAt(0));

  // A background tab, which the active-tab-only upstream item can't reach.
  partners->ActivatedAt(1);
  EXPECT_TRUE(ArePair(b_, c_));
}

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest, MenuSeparatesAPair) {
  OpenThreeTabs();
  Pair(a_, b_);

  for (tabs::TabInterface* half : {a_.get(), b_.get()}) {
    auto menu = MenuFor(half);
    EXPECT_FALSE(FindMenuItem(*menu, u"Open Side by Side With").has_value());
    EXPECT_TRUE(FindMenuItem(*menu, u"Separate Tabs").has_value());
  }

  // The menu's delegate is what turns the click into this call.
  model()->ExecuteContextMenuCommand(model()->GetIndexOfTab(a_),
                                     TabStripModel::CommandAddToSplit);

  EXPECT_FALSE(a_->IsSplit());
  EXPECT_FALSE(b_->IsSplit());
  EXPECT_FALSE(multi_contents_view()->IsInSplitView());
  EXPECT_FALSE(SplitRowFor(a_));
}

// ── Spaces ──────────────────────────────────────────────────────────────────

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       PairOnlyShowsInItsOwnSpace) {
  OpenThreeTabs();
  Pair(a_, b_);
  SplitTabView* split_row = SplitRowFor(a_);
  ASSERT_TRUE(split_row);
  ASSERT_TRUE(split_row->GetVisible());

  const std::string home = ActiveSpace();
  SpaceManager manager(browser()->GetProfile()->GetPrefs());
  const std::string work = manager.CreateSpace("Work");
  GetWindowSpaceState(browser())->SetActiveSpaceId(work);
  EXPECT_TRUE(base::test::RunUntil([&] { return !split_row->GetVisible(); }));
  EXPECT_FALSE(multi_contents_view()->IsInSplitView());

  GetWindowSpaceState(browser())->SetActiveSpaceId(home);
  EXPECT_TRUE(base::test::RunUntil([&] { return split_row->GetVisible(); }));
  EXPECT_TRUE(ArePair(a_, b_));
  EXPECT_TRUE(multi_contents_view()->IsInSplitView());
}

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       SeparatedTabsStayHiddenInAnotherSpace) {
  OpenThreeTabs();
  Pair(a_, b_);
  const std::string home = ActiveSpace();
  SpaceManager manager(browser()->GetProfile()->GetPrefs());
  const std::string work = manager.CreateSpace("Work");
  GetWindowSpaceState(browser())->SetActiveSpaceId(work);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !SplitRowFor(a_)->GetVisible(); }));

  model()->RemoveSplit(*a_->GetSplit());

  EXPECT_TRUE(base::test::RunUntil([&] {
    return RowFor(a_) && !SplitRowFor(a_) && !RowFor(a_)->GetVisible() &&
           !RowFor(b_)->GetVisible();
  }));
}

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest, MovesToASpaceAsOne) {
  OpenThreeTabs();
  Pair(a_, b_);
  SpaceManager manager(browser()->GetProfile()->GetPrefs());
  const std::string work = manager.CreateSpace("Work");

  auto menu = MenuFor(a_);
  const std::optional<size_t> item = FindMenuItem(*menu, u"Move Tab to Space");
  ASSERT_TRUE(item.has_value());
  menu->GetSubmenuModelAt(*item)->ActivatedAt(0);

  EXPECT_EQ(work, GetTabSpaceId(a_->GetContents()));
  EXPECT_EQ(work, GetTabSpaceId(b_->GetContents()));

  // Nothing is waiting to pull them apart.
  Wait(base::Milliseconds(100));
  EXPECT_TRUE(ArePair(a_, b_));
  EXPECT_FALSE(SplitRowFor(a_)->GetVisible());
  EXPECT_EQ(c_, model()->GetActiveTab());
}

// How a move made in another window arrives, where the tab wasn't in a pair.
IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       OneTabLeavingTheSpaceSeparatesThePair) {
  OpenThreeTabs();
  Pair(a_, b_);
  SpaceManager manager(browser()->GetProfile()->GetPrefs());
  const std::string work = manager.CreateSpace("Work");

  SetTabSpaceId(b_->GetContents(), work);
  model()->UpdateWebContentsState(b_->GetContents(), TabChangeType::kAll);

  EXPECT_TRUE(base::test::RunUntil([&] { return !a_->IsSplit(); }));
  EXPECT_FALSE(b_->IsSplit());
}

// ── Staying a pair ──────────────────────────────────────────────────────────

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       NewTabBelowTheLeftTabLandsPastThePair) {
  OpenThreeTabs();
  Pair(a_, b_);
  model()->ExecuteContextMenuCommand(model()->GetIndexOfTab(a_),
                                     TabStripModel::CommandNewTabToRight);

  EXPECT_TRUE(ArePair(a_, b_));
  EXPECT_EQ(4, model()->count());
  EXPECT_EQ(2, model()->active_index());
}

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       LinkOpenedFromTheLeftTabLandsPastThePair) {
  OpenThreeTabs();
  Pair(a_, b_);
  model()->ActivateTabAt(model()->GetIndexOfTab(a_));

  ui_test_utils::NavigateToURLWithDisposition(
      browser(), TestURL("d.test"), WindowOpenDisposition::NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  EXPECT_TRUE(ArePair(a_, b_));
}

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest, InsertionIndexClearsThePair) {
  OpenThreeTabs();
  Pair(a_, b_);
  ASSERT_EQ(0, model()->GetIndexOfTab(a_));
  EXPECT_EQ(0, InsertionIndexOutsideSplit(*model(), 0));
  EXPECT_EQ(2, InsertionIndexOutsideSplit(*model(), 1));
  EXPECT_EQ(2, InsertionIndexOutsideSplit(*model(), 2));
  EXPECT_EQ(3, InsertionIndexOutsideSplit(*model(), 3));
}

// The warm-tab budget unloads the longest-unused background tabs, and the tab
// on the far side of a pair is as background as it gets by that measure while
// being on screen.
IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       WarmTabBudgetLeavesBothHalvesAlone) {
  OpenThreeTabs();
  Pair(a_, b_);
  for (int i = 0; i < 14; ++i) {
    OpenBackgroundTab(TestURL("more" + base::NumberToString(i) + ".test"));
  }
  ASSERT_TRUE(base::test::RunUntil([&] {
    return model()->GetTabAtIndex(model()->count() - 1)->GetContents()
        ->GetLastCommittedURL()
        .is_valid();
  }));
  Wait(base::Milliseconds(200));
  EXPECT_FALSE(a_->GetContents()->GetController().NeedsReload());
  EXPECT_FALSE(b_->GetContents()->GetController().NeedsReload());
}

// ── Link previews, which need the pair's second pane ────────────────────────

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       PreviewFromEitherTabStepsThePairAside) {
  OpenThreeTabs();
  Pair(a_, b_);

  for (tabs::TabInterface* host : {a_.get(), b_.get()}) {
    model()->ActivateTabAt(model()->GetIndexOfTab(host));
    ASSERT_EQ(SecondaryContentsMode::kSplit,
              multi_contents_view()->secondary_contents_mode());

    const int tabs_before = model()->count();
    ASSERT_TRUE(content::ExecJs(host->GetContents(),
                                "document.getElementById('go').click();"));
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return model()->count() == tabs_before + 1; }));
    content::WebContents* const preview =
        GetLinkPreviewContents(host->GetContents());
    ASSERT_TRUE(preview);
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return model()->GetActiveWebContents() == preview; }));

    EXPECT_EQ(SecondaryContentsMode::kOverlay,
              multi_contents_view()->secondary_contents_mode());
    EXPECT_EQ(host->GetContents(), multi_contents_view()
                                       ->GetPrimaryContentsContainerView()
                                       ->contents_view()
                                       ->web_contents());
    EXPECT_TRUE(ArePair(a_, b_));

    browser_view()->HideAvoraLinkPreview();
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return model()->GetActiveTab() == host; }));
    EXPECT_EQ(SecondaryContentsMode::kSplit,
              multi_contents_view()->secondary_contents_mode());
    EXPECT_TRUE(ArePair(a_, b_));
  }
}

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       ParkedPreviewRemountsOverThePair) {
  OpenThreeTabs();
  Pair(a_, b_);
  model()->ActivateTabAt(model()->GetIndexOfTab(a_));
  const int tabs_before = model()->count();
  ASSERT_TRUE(content::ExecJs(a_->GetContents(),
                              "document.getElementById('go').click();"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return model()->count() == tabs_before + 1; }));
  content::WebContents* const preview =
      GetLinkPreviewContents(a_->GetContents());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return model()->GetActiveWebContents() == preview; }));

  // Away to another tab parks the preview; back to the host remounts it, from
  // inside OnActiveTabChanged(), with the pair already showing.
  model()->ActivateTabAt(model()->GetIndexOfTab(c_));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return multi_contents_view()->secondary_contents_mode() ==
           SecondaryContentsMode::kNone;
  }));
  model()->ActivateTabAt(model()->GetIndexOfTab(a_));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return multi_contents_view()->secondary_contents_mode() ==
           SecondaryContentsMode::kOverlay;
  }));
  EXPECT_TRUE(ArePair(a_, b_));
}

// ── Dragging ────────────────────────────────────────────────────────────────

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       HoldingATabOverAnotherCombinesThem) {
  OpenThreeTabs();
  PrepareForInput();

  StartDragging(a_, kStartDrag);
  GlideMouseToCenterOf(c_);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return RowFor(c_)->avora_combine_highlight();
  })) << "the row should light up while the drag rests on it";
  // The dragged row is on top of the lit one, so it has to get out of the way
  // or the highlight can't be seen.
  EXPECT_LT(RowFor(a_)->layer()->opacity(), 1.f);
  Release();

  EXPECT_TRUE(base::test::RunUntil([&] { return a_->IsSplit(); }));
  EXPECT_TRUE(ArePair(c_, a_));
  EXPECT_TRUE(multi_contents_view()->IsInSplitView());
  EXPECT_EQ(a_, model()->GetActiveTab());
  EXPECT_FALSE(RowFor(c_)->avora_combine_highlight());
}

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       LettingGoBeforeItLightsUpJustReorders) {
  OpenThreeTabs();
  PrepareForInput();

  StartDragging(a_, kStartDrag);
  GlideMouseToCenterOf(b_);
  Release();

  Wait(base::Milliseconds(100));
  EXPECT_FALSE(a_->IsSplit());
  EXPECT_FALSE(multi_contents_view()->IsInSplitView());
}

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       DraggingPastARowStillReorders) {
  OpenThreeTabs();
  PrepareForInput();

  // Beyond the middle of b, so past where it would combine.
  StartDragging(a_, kStartDrag);
  GlideMouseNearBottomOf(b_);
  Release();

  EXPECT_FALSE(a_->IsSplit());
  EXPECT_EQ(0, model()->GetIndexOfTab(b_));
  EXPECT_EQ(1, model()->GetIndexOfTab(a_));
}

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest, EscapeCancelsTheCombine) {
  OpenThreeTabs();
  PrepareForInput();

  StartDragging(a_, kStartDrag);
  GlideMouseToCenterOf(c_);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return RowFor(c_)->avora_combine_highlight(); }));
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_ESCAPE, false,
                                              false, false, false));
  Release();

  Wait(base::Milliseconds(100));
  EXPECT_FALSE(a_->IsSplit());
  EXPECT_FALSE(c_->IsSplit());
  EXPECT_FALSE(RowFor(c_)->avora_combine_highlight());
}

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       MovingOffTheRowBeforeLettingGoCancelsTheCombine) {
  OpenThreeTabs();
  PrepareForInput();

  StartDragging(a_, kStartDrag);
  GlideMouseToCenterOf(c_);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return RowFor(c_)->avora_combine_highlight(); }));
  GlideMouseNearBottomOf(c_);
  EXPECT_FALSE(RowFor(c_)->avora_combine_highlight());
  EXPECT_EQ(1.f, RowFor(a_)->layer()->opacity());
  Release();

  Wait(base::Milliseconds(100));
  EXPECT_FALSE(a_->IsSplit());
}

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest, APairTakesNoThirdTab) {
  OpenThreeTabs();
  Pair(a_, b_);
  PrepareForInput();

  StartDragging(c_, kStartDrag);
  GlideMouse([&] { return SplitRowFor(a_)->GetBoundsInScreen().CenterPoint(); });
  Wait(base::Milliseconds(450));
  Release();

  Wait(base::Milliseconds(100));
  EXPECT_TRUE(ArePair(a_, b_));
  EXPECT_FALSE(c_->IsSplit());
}

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       DraggingOneTabOffThePairSeparatesIt) {
  OpenThreeTabs();
  Pair(a_, b_);
  PrepareForInput();

  // Clear of the pair's row, which is what takes it apart.
  StartDragging(a_, gfx::Vector2d(0, 30));
  EXPECT_FALSE(a_->IsSplit());
  EXPECT_FALSE(b_->IsSplit());
  EXPECT_FALSE(multi_contents_view()->IsInSplitView());

  GlideMouseNearBottomOf(c_);
  Release();

  // Only the tab that was pulled moved, and the drag kept hold of it the whole
  // way down.
  EXPECT_EQ(0, model()->GetIndexOfTab(b_));
  EXPECT_EQ(1, model()->GetIndexOfTab(c_));
  EXPECT_EQ(2, model()->GetIndexOfTab(a_));
  EXPECT_EQ(a_, model()->GetActiveTab());
}

// The same, from the half that isn't active.
IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       DraggingTheInactiveTabOffThePairSeparatesIt) {
  OpenThreeTabs();
  Pair(a_, b_);
  model()->ActivateTabAt(model()->GetIndexOfTab(b_));
  PrepareForInput();

  StartDragging(a_, gfx::Vector2d(0, 30));
  GlideMouseNearBottomOf(c_);
  Release();

  EXPECT_FALSE(a_->IsSplit());
  EXPECT_EQ(0, model()->GetIndexOfTab(b_));
  EXPECT_EQ(2, model()->GetIndexOfTab(a_));
}

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       ASlippedClickLeavesThePairAlone) {
  OpenThreeTabs();
  Pair(a_, b_);
  PrepareForInput();

  Press(CenterOf(a_));
  GlideMouseBy(gfx::Vector2d(6, 4));
  Release();

  EXPECT_TRUE(ArePair(a_, b_));
}

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       ClickingTheOtherTabOfAPairSwitchesToIt) {
  OpenThreeTabs();
  Pair(a_, b_);
  PrepareForInput();
  ASSERT_EQ(b_, model()->GetActiveTab());

  Press(CenterOf(a_));
  Release();

  EXPECT_TRUE(
      base::test::RunUntil([&] { return model()->GetActiveTab() == a_; }));
  EXPECT_TRUE(ArePair(a_, b_));
}

IN_PROC_BROWSER_TEST_F(AvoraSideBySideBrowserTest,
                       APulledOutTabCanBeCombinedAgain) {
  OpenThreeTabs();
  Pair(a_, b_);
  PrepareForInput();

  StartDragging(a_, gfx::Vector2d(0, 30));
  GlideMouseToCenterOf(c_);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return RowFor(c_)->avora_combine_highlight(); }));
  Release();

  EXPECT_TRUE(base::test::RunUntil([&] { return a_->IsSplit(); }));
  EXPECT_TRUE(ArePair(c_, a_));
  EXPECT_FALSE(b_->IsSplit());
}

}  // namespace avora
