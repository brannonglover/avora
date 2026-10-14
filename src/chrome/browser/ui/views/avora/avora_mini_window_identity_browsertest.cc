// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_mini_window.h"

#include <string>

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/browser/avora/avora_profile.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_storage_partition.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/window_controller.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/base_window.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace avora {

// Checkpoint 1: the mini window's window and tab identity.
//
// The mini window used to hold a WebContents that no TabStripModel owned and
// no window controller knew about, which is why extensions could not see the
// page it was showing: ExtensionTabUtil::GetTabById only finds tabs in the
// strips of the windows in WindowControllerList.  These tests pin down that
// the page is now a real tab in a real window, that the window stays a
// one-page window, that its Space identity survives navigation and reuse, and
// that closing it leaves nothing stale behind.
class AvoraMiniWindowIdentityBrowserTest : public InProcessBrowserTest {
 public:
  AvoraMiniWindowIdentityBrowserTest() = default;

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  void TearDownOnMainThread() override {
    CloseMiniWindow();
    base::RunLoop().RunUntilIdle();
    InProcessBrowserTest::TearDownOnMainThread();
  }

 protected:
  Profile* profile() { return browser()->GetProfile(); }

  GURL TestURL(const std::string& host, const std::string& path) {
    return embedded_test_server()->GetURL(host, path);
  }

  // Opens the mini window and waits for its tab to exist.
  BrowserWindowInterface* OpenMiniWindow(const GURL& url) {
    ShowMiniWindow(profile(), url);
    if (!base::test::RunUntil([&]() {
          BrowserWindowInterface* mini = GetMiniWindowBrowser();
          return mini && mini->GetTabStripModel()->GetActiveWebContents();
        })) {
      return nullptr;
    }
    return GetMiniWindowBrowser();
  }

  content::WebContents* MiniWindowContents() {
    BrowserWindowInterface* mini = GetMiniWindowBrowser();
    return mini ? mini->GetTabStripModel()->GetActiveWebContents() : nullptr;
  }

  // Creates a Space with an identity of its own and makes it the Space the
  // main window is showing, which is the Space a mini window then adopts.
  std::string CreateAndActivateSpace(const std::string& name) {
    SpaceManager manager(profile()->GetPrefs());
    const std::string id = manager.CreateSpace(name);
    WindowSpaceState* state = GetWindowSpaceState(browser());
    EXPECT_TRUE(state);
    if (state) {
      state->SetActiveSpaceId(id);
    }
    return id;
  }

  // The partition |space_id|'s identity owns.
  content::StoragePartitionConfig PartitionForSpace(
      const std::string& space_id) {
    SpaceManager manager(profile()->GetPrefs());
    const Space* space = manager.GetSpaceById(space_id);
    EXPECT_TRUE(space);
    BrowserProfileStore store(profile()->GetPrefs());
    const BrowserProfile* browser_profile =
        space ? store.GetProfileById(space->profile_id) : nullptr;
    EXPECT_TRUE(browser_profile);
    return GetPartitionConfigForProfile(profile(), *browser_profile);
  }

  // The partition |contents| is actually reading and writing through, which is
  // what matters for cookies and logins.
  content::StoragePartitionConfig PartitionOf(content::WebContents* contents) {
    return contents->GetPrimaryMainFrame()->GetStoragePartition()->GetConfig();
  }
};

// The shape of the window: a popup browser holding exactly one tab, marked as
// a mini window, with the user's tabbed window left alone.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowIdentityBrowserTest,
                       OpensAsAPopupBrowserWithOneTab) {
  const int tabs_before = browser()->GetTabStripModel()->count();

  BrowserWindowInterface* mini = OpenMiniWindow(TestURL("a.test", "/title1.html"));
  ASSERT_TRUE(mini);

  EXPECT_EQ(BrowserWindowInterface::TYPE_POPUP, mini->GetType());
  EXPECT_EQ(1, mini->GetTabStripModel()->count());
  EXPECT_TRUE(IsMiniWindow(mini));
  EXPECT_TRUE(IsMiniWindowShowing());

  // The page the user was already working in is untouched.
  EXPECT_FALSE(IsMiniWindow(browser()));
  EXPECT_EQ(tabs_before, browser()->GetTabStripModel()->count());
}

// The bug this checkpoint exists to fix: an extension asking about the page in
// the mini window by tab id must find it.  GetTabById walks the tab strips of
// the windows in WindowControllerList, so this fails for any page that is not
// a tab in a window extensions can see.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowIdentityBrowserTest,
                       PageIsATabExtensionsCanResolve) {
  ASSERT_TRUE(OpenMiniWindow(TestURL("a.test", "/title1.html")));
  content::WebContents* contents = MiniWindowContents();
  ASSERT_TRUE(contents);

  const int tab_id = sessions::SessionTabHelper::IdForTab(contents).id();
  EXPECT_NE(-1, tab_id);

  extensions::WindowController* window = nullptr;
  content::WebContents* resolved = nullptr;
  int tab_index = -1;
  EXPECT_TRUE(extensions::ExtensionTabUtil::GetTabById(
      tab_id, profile(), /*include_incognito=*/false, &window, &resolved,
      &tab_index));
  EXPECT_EQ(contents, resolved);
  // A window controller is what chrome.windows and chrome.action.openPopup
  // resolve through; the old free-standing page had none.
  EXPECT_TRUE(window);
  EXPECT_EQ(0, tab_index);
}

// A second external link re-points the open window rather than stacking
// another one on screen.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowIdentityBrowserTest,
                       ReusesTheOpenWindowForTheSameSpace) {
  BrowserWindowInterface* first =
      OpenMiniWindow(TestURL("a.test", "/title1.html"));
  ASSERT_TRUE(first);

  const GURL second_url = TestURL("b.test", "/title2.html");
  ShowMiniWindow(profile(), second_url);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    content::WebContents* contents = MiniWindowContents();
    return contents && contents->GetLastCommittedURL() == second_url;
  }));

  EXPECT_EQ(first, GetMiniWindowBrowser());
  EXPECT_EQ(1, first->GetTabStripModel()->count());
}

// The identity the window is pinned to has to survive the navigations the page
// makes afterwards -- the initial SiteInstance choice is not on its own a
// guarantee, so this follows the tab across a cross-site navigation.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowIdentityBrowserTest,
                       TabKeepsItsSpacePartitionAcrossNavigation) {
  const std::string space = CreateAndActivateSpace("Work");
  const content::StoragePartitionConfig expected = PartitionForSpace(space);
  // A Space with an identity of its own must not be sharing the default
  // partition, or this test would pass without isolating anything.
  ASSERT_FALSE(expected.is_default());

  ASSERT_TRUE(OpenMiniWindow(TestURL("a.test", "/title1.html")));
  content::WebContents* contents = MiniWindowContents();
  ASSERT_TRUE(contents);
  EXPECT_EQ(space, MiniWindowController::Get()->space_id());
  EXPECT_EQ(expected, PartitionOf(contents));

  // Cross-site, in the same tab: a new SiteInstance, which must still be in
  // the Space's partition.
  ASSERT_TRUE(content::NavigateToURL(contents, TestURL("b.test", "/title2.html")));
  EXPECT_EQ(expected, PartitionOf(contents));
}

// Reuse is only reuse while the identity still matches.  Once the user has
// moved to another Space, the next link belongs to that Space, and a tab
// already pinned to the old one cannot host it.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowIdentityBrowserTest,
                       ReplacesTheWindowWhenTheSpaceHasChanged) {
  const std::string first_space = CreateAndActivateSpace("Work");
  BrowserWindowInterface* first =
      OpenMiniWindow(TestURL("a.test", "/title1.html"));
  ASSERT_TRUE(first);
  ASSERT_EQ(first_space, MiniWindowController::Get()->space_id());
  const size_t windows_with_one_mini =
      GlobalBrowserCollection::GetInstance()->GetSize();

  const std::string second_space = CreateAndActivateSpace("Personal");
  const content::StoragePartitionConfig expected =
      PartitionForSpace(second_space);

  const GURL second_url = TestURL("b.test", "/title2.html");
  ShowMiniWindow(profile(), second_url);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    BrowserWindowInterface* mini = GetMiniWindowBrowser();
    return mini && mini != first &&
           mini->GetTabStripModel()->GetActiveWebContents();
  }));

  EXPECT_EQ(second_space, MiniWindowController::Get()->space_id());
  EXPECT_EQ(expected, PartitionOf(MiniWindowContents()));

  // The window it replaced goes away rather than lingering behind it: one
  // window opened, one closed, so the count is where it was.  Counted rather
  // than compared against |first|, which by now may be destroyed.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return GlobalBrowserCollection::GetInstance()->GetSize() ==
           windows_with_one_mini;
  }));
}

// A link the page opens in a new tab has nowhere to go in a one-page window,
// and must not be dropped on the floor: it belongs in the user's working set.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowIdentityBrowserTest,
                       NewTabFromThePageGoesToTheTabbedWindow) {
  BrowserWindowInterface* mini =
      OpenMiniWindow(TestURL("a.test", "/title1.html"));
  ASSERT_TRUE(mini);
  const int tabs_before = browser()->GetTabStripModel()->count();

  // What a target="_blank" link in the page asks for.
  const GURL opened = TestURL("b.test", "/title2.html");
  NavigateParams params(mini, opened, ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);

  EXPECT_EQ(1, mini->GetTabStripModel()->count());
  EXPECT_EQ(tabs_before + 1, browser()->GetTabStripModel()->count());
  EXPECT_EQ(opened,
            browser()->GetTabStripModel()->GetActiveWebContents()->GetVisibleURL());
}

// Committing the page to a Space opens it there and takes the glance away.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowIdentityBrowserTest,
                       SendToSpaceOpensATabAndClosesTheWindow) {
  const std::string space = CreateAndActivateSpace("Work");
  const GURL url = TestURL("a.test", "/title1.html");
  ASSERT_TRUE(OpenMiniWindow(url));
  const int tabs_before = browser()->GetTabStripModel()->count();

  SendMiniWindowToSpace(space);

  EXPECT_FALSE(IsMiniWindowShowing());
  EXPECT_EQ(tabs_before + 1, browser()->GetTabStripModel()->count());
  EXPECT_EQ(url,
            browser()->GetTabStripModel()->GetActiveWebContents()->GetVisibleURL());
}

// Closing must take effect at once as far as the rest of Avora is concerned,
// even though the window itself goes away asynchronously: a window on its way
// out must never be handed back or reused.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowIdentityBrowserTest,
                       ClosingLeavesNothingStaleBehind) {
  ASSERT_TRUE(OpenMiniWindow(TestURL("a.test", "/title1.html")));

  CloseMiniWindow();
  EXPECT_FALSE(IsMiniWindowShowing());
  EXPECT_EQ(nullptr, GetMiniWindowBrowser());
  EXPECT_EQ(nullptr, MiniWindowController::Get());

  base::RunLoop().RunUntilIdle();

  // And the next link opens a window again rather than finding a dead one.
  BrowserWindowInterface* second =
      OpenMiniWindow(TestURL("b.test", "/title2.html"));
  ASSERT_TRUE(second);
  EXPECT_TRUE(IsMiniWindowShowing());
}

}  // namespace avora
