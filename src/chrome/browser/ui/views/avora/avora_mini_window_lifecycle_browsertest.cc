// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_mini_window.h"

#include <string>

#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_restore_test_helper.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/sessions/session_service_test_helper.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/base_window.h"
#include "url/gurl.h"

namespace avora {

// Checkpoint 2 regressions: how the mini window behaves alongside the rest of
// the windows on screen, now that it is a browser window itself.
//
// A browser window comes with machinery the free-standing widget never had to
// answer for -- session restore wants to bring windows back, and the window
// the page opens with window.open() is a sibling popup that must not be
// mistaken for the mini window.
class AvoraMiniWindowLifecycleBrowserTest : public InProcessBrowserTest {
 public:
  AvoraMiniWindowLifecycleBrowserTest() = default;

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

  // Closes |browser| and starts a new session, which restores the previous
  // one.  Modelled on SessionRestoreTest's own helper, which is private to its
  // test file.
  void QuitAndRestore(BrowserWindowInterface* last_browser) {
    Profile* const restore_profile = last_browser->GetProfile();

    auto keep_alive = std::make_unique<ScopedKeepAlive>(
        KeepAliveOrigin::SESSION_RESTORE, KeepAliveRestartOption::DISABLED);
    auto profile_keep_alive = std::make_unique<ScopedProfileKeepAlive>(
        restore_profile, ProfileKeepAliveOrigin::kBrowserWindow);
    CloseBrowserSynchronously(last_browser);

    ui_test_utils::AllBrowserTabAddedWaiter tab_waiter;
    SessionRestoreTestHelper restore_observer;

    SessionServiceTestHelper helper(restore_profile);
    helper.SetForceBrowserNotAliveWithNoWindows(true);

    chrome::NewEmptyWindow(restore_profile);
    tab_waiter.Wait();
    restore_observer.Wait();
  }
};

// A glance is not part of the session.  The mini window asks to be omitted
// from session restore; this is the proof that the ask lands -- the tabbed
// window comes back after a restart and the mini window does not.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowLifecycleBrowserTest,
                       IsNotRestoredWithTheSession) {
  const GURL kept = TestURL("a.test", "/title1.html");
  const GURL glanced = TestURL("b.test", "/title2.html");

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), kept));
  ASSERT_TRUE(OpenMiniWindow(glanced));
  ASSERT_EQ(2u, GlobalBrowserCollection::GetInstance()->GetSize());

  // Shut the session service down so closing the windows below reads as a
  // restart rather than as the user tidying up.
  SessionServiceFactory::ShutdownForProfile(profile());
  CloseMiniWindow();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return GlobalBrowserCollection::GetInstance()->GetSize() == 1u; }));

  QuitAndRestore(browser());

  // Exactly one window came back, and it is the tabbed one with the page the
  // user was actually working on.
  EXPECT_EQ(1u, GlobalBrowserCollection::GetInstance()->GetSize());
  EXPECT_FALSE(IsMiniWindowShowing());
  BrowserWindowInterface* const restored =
      GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
  ASSERT_TRUE(restored);
  EXPECT_EQ(BrowserWindowInterface::TYPE_NORMAL, restored->GetType());
  EXPECT_FALSE(IsMiniWindow(restored));
  bool has_kept_page = false;
  for (int i = 0; i < restored->GetTabStripModel()->count(); ++i) {
    if (restored->GetTabStripModel()->GetWebContentsAt(i)->GetVisibleURL() ==
        kept) {
      has_kept_page = true;
    }
  }
  EXPECT_TRUE(has_kept_page);
}

// A page in the mini window asking for a sized window gets a real popup of its
// own -- an OAuth or payment flow depends on it -- and that popup must not be
// confused with the mini window, which stays exactly as it was.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowLifecycleBrowserTest,
                       WindowOpenWithFeaturesMakesItsOwnPopup) {
  BrowserWindowInterface* mini =
      OpenMiniWindow(TestURL("a.test", "/title1.html"));
  ASSERT_TRUE(mini);
  const size_t windows_before =
      GlobalBrowserCollection::GetInstance()->GetSize();

  const GURL opened = TestURL("b.test", "/title2.html");
  ASSERT_TRUE(content::ExecJs(
      mini->GetTabStripModel()->GetActiveWebContents(),
      content::JsReplace("window.open($1, '_blank', 'width=400,height=300');",
                         opened)));

  // Waits for the window *and* its page: the window appears in the collection
  // before its tab has a URL to report, so asserting on the two separately
  // races the navigation.
  BrowserWindowInterface* opened_window = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    if (GlobalBrowserCollection::GetInstance()->GetSize() !=
        windows_before + 1) {
      return false;
    }
    opened_window = nullptr;
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* candidate) {
          if (candidate != mini && candidate != browser() &&
              !candidate->IsDeleteScheduled()) {
            opened_window = candidate;
            return false;
          }
          return true;
        });
    if (!opened_window) {
      return false;
    }
    content::WebContents* contents =
        opened_window->GetTabStripModel()->GetActiveWebContents();
    return contents && contents->GetVisibleURL() == opened;
  }));

  // The mini window is untouched: same window, same single tab, still the one
  // the singleton tracks.
  EXPECT_EQ(mini, GetMiniWindowBrowser());
  EXPECT_TRUE(IsMiniWindowShowing());
  EXPECT_EQ(1, mini->GetTabStripModel()->count());
  EXPECT_TRUE(IsMiniWindow(mini));

  // And the window the page opened is a popup of its own, not a mini window.
  ASSERT_TRUE(opened_window);
  EXPECT_FALSE(IsMiniWindow(opened_window));
  EXPECT_EQ(BrowserWindowInterface::TYPE_POPUP, opened_window->GetType());
}

// Closing the mini window takes the mini window and nothing else: a popup the
// page opened is a window in its own right and outlives the glance.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowLifecycleBrowserTest,
                       ClosingLeavesOtherPopupsOpen) {
  BrowserWindowInterface* mini =
      OpenMiniWindow(TestURL("a.test", "/title1.html"));
  ASSERT_TRUE(mini);

  // A plain popup alongside it, the kind window.open() produces.
  BrowserWindowCreateParams params(BrowserWindowInterface::TYPE_POPUP,
                                   profile(), /*from_user_gesture=*/true);
  BrowserWindowInterface* popup = CreateBrowserWindow(std::move(params));
  ASSERT_TRUE(popup);
  content::WebContents* popup_contents = chrome::AddSelectedTabWithURL(
      popup, TestURL("b.test", "/title2.html"),
      ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  ASSERT_TRUE(popup_contents);
  popup->GetWindow()->Show();
  EXPECT_FALSE(IsMiniWindow(popup));

  CloseMiniWindow();
  EXPECT_FALSE(IsMiniWindowShowing());
  base::RunLoop().RunUntilIdle();

  // The popup is still there, still holding its page.
  EXPECT_FALSE(popup->IsDeleteScheduled());
  EXPECT_EQ(1, popup->GetTabStripModel()->count());

  // And a new glance opens cleanly beside it rather than adopting it.
  BrowserWindowInterface* second =
      OpenMiniWindow(TestURL("a.test", "/title1.html"));
  ASSERT_TRUE(second);
  EXPECT_NE(popup, second);
  EXPECT_TRUE(IsMiniWindow(second));
  EXPECT_FALSE(IsMiniWindow(popup));

  CloseBrowserSynchronously(popup);
}

}  // namespace avora
