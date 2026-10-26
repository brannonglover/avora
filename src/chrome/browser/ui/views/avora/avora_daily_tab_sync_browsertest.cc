// Copyright 2026 Avora. All rights reserved.

#include <set>
#include <string>
#include <vector>

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_tab_guid.h"
#include "chrome/browser/avora/avora_tab_space.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace avora {

// A Space's daily list is one list across all of the profile's windows.
// These tests pin down the two things that promise is made of: a new window
// opens on the list its Space already has, and a tab opened, closed, or
// navigated in one window shows up that way in the others -- with the copies
// in the other windows left unloaded until the user opens them there.
class AvoraDailyTabSyncBrowserTest : public InProcessBrowserTest {
 public:
  AvoraDailyTabSyncBrowserTest() = default;

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  GURL TestURL(const std::string& host) {
    return embedded_test_server()->GetURL(host, "/title1.html");
  }

  TabStripModel* model(BrowserWindowInterface* window) {
    return window->GetTabStripModel();
  }

  std::string ActiveSpace(BrowserWindowInterface* window) {
    WindowSpaceState* state = GetWindowSpaceState(window);
    return state ? state->active_space_id() : std::string();
  }

  // Opens a window the way Cmd+N does and returns it.
  BrowserWindowInterface* OpenNewWindow(BrowserWindowInterface* from) {
    const std::vector<BrowserWindowInterface*> before =
        GetAllBrowserWindowInterfaces();
    chrome::ExecuteCommand(from, IDC_NEW_WINDOW);
    for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
      if (!std::ranges::contains(before, window) &&
          window->GetType() == BrowserWindowInterface::TYPE_NORMAL) {
        return window;
      }
    }
    return nullptr;
  }

  // Opens |url| in a new background tab of |window| and waits for it to load.
  content::WebContents* OpenBackgroundTab(BrowserWindowInterface* window,
                                          const GURL& url) {
    ui_test_utils::NavigateToURLWithDisposition(
        window, url, WindowOpenDisposition::NEW_BACKGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
    return FindTab(window, url);
  }

  // The tab in |window| showing, or set to show, |url|.
  content::WebContents* FindTab(BrowserWindowInterface* window,
                                const GURL& url) {
    for (int i = 0; i < model(window)->count(); ++i) {
      content::WebContents* contents = model(window)->GetWebContentsAt(i);
      if (contents->GetVisibleURL() == url) {
        return contents;
      }
    }
    return nullptr;
  }

  // The URLs of |space_id|'s tabs in |window|, in strip order.
  std::vector<GURL> SpaceUrls(BrowserWindowInterface* window,
                              const std::string& space_id) {
    std::vector<GURL> urls;
    for (int i = 0; i < model(window)->count(); ++i) {
      content::WebContents* contents = model(window)->GetWebContentsAt(i);
      if (GetTabSpaceId(contents) == space_id) {
        urls.push_back(contents->GetVisibleURL());
      }
    }
    return urls;
  }

  // Waits for the other windows to have caught up with a change.
  bool WaitForTabIn(BrowserWindowInterface* window, const GURL& url) {
    return base::test::RunUntil([&] { return FindTab(window, url); });
  }
  bool WaitForNoTabIn(BrowserWindowInterface* window, const GURL& url) {
    return base::test::RunUntil([&] { return !FindTab(window, url); });
  }
};

// Cmd+N opens on the Space's daily tabs, in the same order, with the tab the
// user was on selected and no extra blank tab -- and none of the background
// copies has loaded anything.
IN_PROC_BROWSER_TEST_F(AvoraDailyTabSyncBrowserTest,
                       NewWindowCarriesOverDailyTabs) {
  const GURL a = TestURL("a.test");
  const GURL b = TestURL("b.test");
  const GURL c = TestURL("c.test");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), a));
  OpenBackgroundTab(browser(), b);
  OpenBackgroundTab(browser(), c);
  const std::string space = ActiveSpace(browser());
  ASSERT_FALSE(space.empty());

  // Select b rather than a, the first tab, so the new window's selection can
  // only have come from this one.
  model(browser())->ActivateTabAt(model(browser())->GetIndexOfWebContents(
      FindTab(browser(), b)));

  BrowserWindowInterface* second = OpenNewWindow(browser());
  ASSERT_TRUE(second);

  EXPECT_EQ(space, ActiveSpace(second));
  ASSERT_EQ(3u, SpaceUrls(browser(), space).size());
  EXPECT_EQ(SpaceUrls(browser(), space), SpaceUrls(second, space));
  EXPECT_EQ(3, model(second)->count());
  EXPECT_EQ(b, model(second)->GetActiveWebContents()->GetVisibleURL());

  // Each copy is the same logical tab as its original.
  for (const GURL& url : {a, b, c}) {
    EXPECT_EQ(GetTabGuid(FindTab(browser(), url)),
              GetTabGuid(FindTab(second, url)))
        << url;
  }

  // The copies the user has not opened are still waiting to load.
  EXPECT_TRUE(FindTab(second, a)->GetController().NeedsReload());
  EXPECT_TRUE(FindTab(second, c)->GetController().NeedsReload());

  // Nothing came back the other way: the first window is as it was.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(3, model(browser())->count());
}

// A new window opens on the Space of the window it was opened from, and its
// carried tabs sign in as that Space's identity.
IN_PROC_BROWSER_TEST_F(AvoraDailyTabSyncBrowserTest,
                       NewWindowOpensOnTheSourceWindowsSpace) {
  SpaceManager manager(browser()->GetProfile()->GetPrefs());
  const std::string work = manager.CreateSpace("Work");
  GetWindowSpaceState(browser())->SetActiveSpaceId(work);
  const GURL a = TestURL("a.test");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), a));
  ASSERT_EQ(work, GetTabSpaceId(FindTab(browser(), a)));

  BrowserWindowInterface* second = OpenNewWindow(browser());
  ASSERT_TRUE(second);
  EXPECT_EQ(work, ActiveSpace(second));
  ASSERT_EQ((std::vector<GURL>{a}), SpaceUrls(second, work));

  // Opening the copy loads it, in the Work identity's storage partition.
  content::WebContents* copy = FindTab(second, a);
  ASSERT_EQ(copy, model(second)->GetActiveWebContents());
  ASSERT_TRUE(content::WaitForLoadStop(copy));
  EXPECT_EQ(a, copy->GetLastCommittedURL());
  EXPECT_EQ(FindTab(browser(), a)
                ->GetPrimaryMainFrame()
                ->GetStoragePartition()
                ->GetConfig(),
            copy->GetPrimaryMainFrame()->GetStoragePartition()->GetConfig());
}

// A tab opened in one window appears, unloaded, in the other window of the
// same Space, next to the same neighbour.
IN_PROC_BROWSER_TEST_F(AvoraDailyTabSyncBrowserTest,
                       NewTabAppearsInEveryWindowOfTheSpace) {
  const GURL a = TestURL("a.test");
  const GURL b = TestURL("b.test");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), a));
  BrowserWindowInterface* second = OpenNewWindow(browser());
  ASSERT_TRUE(second);
  const std::string space = ActiveSpace(browser());

  OpenBackgroundTab(browser(), b);
  ASSERT_TRUE(WaitForTabIn(second, b));

  EXPECT_EQ(SpaceUrls(browser(), space), SpaceUrls(second, space));
  content::WebContents* copy = FindTab(second, b);
  EXPECT_EQ(space, GetTabSpaceId(copy));
  EXPECT_EQ(GetTabGuid(FindTab(browser(), b)), GetTabGuid(copy));
  EXPECT_TRUE(copy->GetController().NeedsReload());

  // And the other way round.
  const GURL c = TestURL("c.test");
  OpenBackgroundTab(second, c);
  ASSERT_TRUE(WaitForTabIn(browser(), c));
  EXPECT_EQ(3u, SpaceUrls(browser(), space).size());
  EXPECT_EQ(SpaceUrls(second, space), SpaceUrls(browser(), space));
}

// Closing a tab in one window closes its copy in the others.
IN_PROC_BROWSER_TEST_F(AvoraDailyTabSyncBrowserTest,
                       ClosingATabClosesItEverywhere) {
  const GURL a = TestURL("a.test");
  const GURL b = TestURL("b.test");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), a));
  OpenBackgroundTab(browser(), b);
  BrowserWindowInterface* second = OpenNewWindow(browser());
  ASSERT_TRUE(second);
  ASSERT_TRUE(FindTab(second, b));

  model(browser())->CloseWebContentsAt(
      model(browser())->GetIndexOfWebContents(FindTab(browser(), b)),
      TabCloseTypes::CLOSE_USER_GESTURE);
  ASSERT_TRUE(WaitForNoTabIn(second, b));

  // Only that one: the rest of the list, and the window, are still there.
  EXPECT_TRUE(FindTab(second, a));
  EXPECT_TRUE(FindTab(browser(), a));
}

// A copy the user is not looking at follows its tab to a new page; the copy
// they are looking at is left alone.
IN_PROC_BROWSER_TEST_F(AvoraDailyTabSyncBrowserTest,
                       BackgroundCopiesFollowNavigation) {
  const GURL a = TestURL("a.test");
  const GURL b = TestURL("b.test");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), a));
  OpenBackgroundTab(browser(), b);
  BrowserWindowInterface* second = OpenNewWindow(browser());
  ASSERT_TRUE(second);

  // The second window opened on a's copy, so b's copy is in the background.
  ASSERT_EQ(a, model(second)->GetActiveWebContents()->GetVisibleURL());
  ASSERT_TRUE(content::WaitForLoadStop(model(second)->GetActiveWebContents()));
  const std::string b_guid = GetTabGuid(FindTab(browser(), b));

  // Navigate b, in the first window, somewhere else.
  const GURL b2 = TestURL("b2.test");
  model(browser())->ActivateTabAt(model(browser())->GetIndexOfWebContents(
      FindTab(browser(), b)));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), b2));
  ASSERT_TRUE(WaitForTabIn(second, b2));
  EXPECT_FALSE(FindTab(second, b));
  EXPECT_EQ(b_guid, GetTabGuid(FindTab(second, b2)));
  EXPECT_TRUE(FindTab(second, b2)->GetController().NeedsReload());

  // Now navigate a, which the second window has on screen.
  const GURL a2 = TestURL("a2.test");
  model(browser())->ActivateTabAt(model(browser())->GetIndexOfWebContents(
      FindTab(browser(), a)));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), a2));

  // Let any (wrong) update land before checking it did not.
  OpenBackgroundTab(browser(), TestURL("c.test"));
  ASSERT_TRUE(WaitForTabIn(second, TestURL("c.test")));
  EXPECT_EQ(a, model(second)->GetActiveWebContents()->GetLastCommittedURL());
}

// A window entering a Space for the first time gets that Space's tabs from
// the other windows rather than a blank tab.
IN_PROC_BROWSER_TEST_F(AvoraDailyTabSyncBrowserTest,
                       EnteringASpaceBringsInItsTabs) {
  const std::string home = ActiveSpace(browser());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), TestURL("home.test")));
  BrowserWindowInterface* second = OpenNewWindow(browser());
  ASSERT_TRUE(second);

  // The first window moves to Work and opens two tabs there.
  SpaceManager manager(browser()->GetProfile()->GetPrefs());
  const std::string work = manager.CreateSpace("Work");
  GetWindowSpaceState(browser())->SetActiveSpaceId(work);
  const GURL w1 = TestURL("w1.test");
  const GURL w2 = TestURL("w2.test");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), w1));
  OpenBackgroundTab(browser(), w2);
  ASSERT_EQ(2u, SpaceUrls(browser(), work).size());

  // The second window, still on the original Space, has not been sent them.
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(SpaceUrls(second, work).empty());

  // Until it switches to Work, when it lands on the tab the first window is
  // showing.
  GetWindowSpaceState(second)->SetActiveSpaceId(work);
  EXPECT_EQ(SpaceUrls(browser(), work), SpaceUrls(second, work));
  EXPECT_EQ(w1, model(second)->GetActiveWebContents()->GetVisibleURL());

  // The original Space's list is untouched in both windows.
  EXPECT_EQ((std::vector<GURL>{TestURL("home.test")}),
            SpaceUrls(second, home));
}

// Moving a tab into a new window is a move, not a copy: the new window holds
// the live tab, and the old one keeps a copy, since the list is shared.
IN_PROC_BROWSER_TEST_F(AvoraDailyTabSyncBrowserTest,
                       MovingATabToANewWindowKeepsOneLogicalTab) {
  const GURL a = TestURL("a.test");
  const GURL b = TestURL("b.test");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), a));
  content::WebContents* b_tab = OpenBackgroundTab(browser(), b);
  const std::string b_guid = GetTabGuid(b_tab);
  const std::string space = ActiveSpace(browser());

  model(browser())->ActivateTabAt(model(browser())->GetIndexOfWebContents(b_tab));
  const std::vector<BrowserWindowInterface*> before =
      GetAllBrowserWindowInterfaces();
  chrome::MoveActiveTabToNewWindow(browser());
  BrowserWindowInterface* second = nullptr;
  for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
    if (!std::ranges::contains(before, window)) {
      second = window;
    }
  }
  ASSERT_TRUE(second);

  // The moved tab is the live one in the new window, which also gets the
  // rest of the Space's list.
  ASSERT_TRUE(WaitForTabIn(second, a));
  EXPECT_EQ(b_tab, model(second)->GetActiveWebContents());

  // The first window gets a copy back, under the same GUID.
  ASSERT_TRUE(WaitForTabIn(browser(), b));
  EXPECT_EQ(b_guid, GetTabGuid(FindTab(browser(), b)));

  // One tab per URL in each window.
  for (BrowserWindowInterface* window : {browser(), second}) {
    std::vector<GURL> urls = SpaceUrls(window, space);
    std::ranges::sort(urls);
    EXPECT_EQ((std::vector<GURL>{a, b}), urls);
  }
}

}  // namespace avora
