// Copyright 2026 Avora. All rights reserved.

#include <string>

#include "base/test/run_until.h"
#include "chrome/browser/avora/avora_profile.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_storage_partition.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_quick_nav_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/page_transition_types.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace avora {

// The quick-nav overlay (Cmd+T / Cmd+L) belongs to the tab it was opened from,
// so switching tabs dismisses it rather than leaving it over the new tab.
class AvoraQuickNavBrowserTest : public InProcessBrowserTest {
 protected:
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

  TabStripModel* model() { return browser()->tab_strip_model(); }

  // BrowserView keeps the overlay in a private member, so find it among its
  // children instead of adding an accessor to a widely included header.
  AvoraQuickNavView* quick_nav() {
    for (views::View* child : browser_view()->children()) {
      if (auto* view = views::AsViewClass<AvoraQuickNavView>(child)) {
        return view;
      }
    }
    return nullptr;
  }
};

// The overlay's commands: "Clear Cache" and "Clear Cookies" act on the site of
// the page the overlay was opened over, in that page's Space, then reload it.
class AvoraQuickNavCommandBrowserTest : public AvoraQuickNavBrowserTest {
 public:
  void SetUpOnMainThread() override {
    AvoraQuickNavBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  Profile* profile() { return browser()->GetProfile(); }

  GURL TestURL(const std::string& host) {
    return embedded_test_server()->GetURL(host, "/title1.html");
  }

  // Opens |host| in a new foreground tab and sets a cookie there from the page
  // itself, so it lands in whatever partition the tab really uses.
  content::WebContents* OpenSiteWithCookie(const std::string& host,
                                           const std::string& cookie) {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), TestURL(host), WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    content::WebContents* contents = model()->GetActiveWebContents();
    EXPECT_TRUE(content::ExecJs(contents, "document.cookie = '" + cookie +
                                              "; max-age=3600';"));
    EXPECT_EQ(cookie, content::EvalJs(contents, "document.cookie"));
    return contents;
  }

  content::StoragePartitionConfig PartitionOf(content::WebContents* contents) {
    return contents->GetPrimaryMainFrame()->GetStoragePartition()->GetConfig();
  }

  // Types |text| into the overlay and presses Enter, as the user would.
  void TypeAndEnter(const std::u16string& text) {
    views::Textfield* field = FindTextfield(quick_nav());
    ASSERT_TRUE(field);
    field->SetText(text);
    quick_nav()->HandleKeyEvent(
        field, ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_RETURN,
                            ui::EF_NONE));
  }

  // Runs |text| over |contents| and waits for the reload that follows.
  void RunCommandOn(content::WebContents* contents,
                    const std::u16string& text) {
    content::TestNavigationObserver reload(contents);
    TypeAndEnter(text);
    reload.Wait();
  }

 private:
  static views::Textfield* FindTextfield(views::View* view) {
    if (auto* field = views::AsViewClass<views::Textfield>(view)) {
      return field;
    }
    for (views::View* child : view->children()) {
      if (views::Textfield* field = FindTextfield(child)) {
        return field;
      }
    }
    return nullptr;
  }
};

IN_PROC_BROWSER_TEST_F(AvoraQuickNavBrowserTest, HidesWhenAnotherTabActivates) {
  ASSERT_TRUE(AddTabAtIndex(1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  model()->ActivateTabAt(0);
  ASSERT_TRUE(quick_nav());

  browser_view()->ShowAvoraQuickNav();
  ASSERT_TRUE(quick_nav()->IsShowing());

  model()->ActivateTabAt(1);
  EXPECT_FALSE(quick_nav()->IsShowing());
}

IN_PROC_BROWSER_TEST_F(AvoraQuickNavBrowserTest,
                       UrlEditHidesWhenAnotherTabActivates) {
  ASSERT_TRUE(AddTabAtIndex(1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  model()->ActivateTabAt(0);
  ASSERT_TRUE(quick_nav());

  browser_view()->ShowAvoraQuickNavWithURL();
  ASSERT_TRUE(quick_nav()->IsShowing());

  model()->ActivateTabAt(1);
  EXPECT_FALSE(quick_nav()->IsShowing());

  // Coming back to the original tab does not bring it back either.
  model()->ActivateTabAt(0);
  EXPECT_FALSE(quick_nav()->IsShowing());
}

IN_PROC_BROWSER_TEST_F(AvoraQuickNavCommandBrowserTest,
                       ClearCookiesClearsOnlyTheCurrentSite) {
  content::WebContents* other = OpenSiteWithCookie("b.test", "b=1");
  content::WebContents* page = OpenSiteWithCookie("a.test", "a=1");

  browser_view()->ShowAvoraQuickNavWithURL();
  ASSERT_TRUE(quick_nav()->IsShowing());
  RunCommandOn(page, u"Clear Cookies");

  EXPECT_FALSE(quick_nav()->IsShowing());
  EXPECT_EQ(page, model()->GetActiveWebContents());
  EXPECT_EQ("", content::EvalJs(page, "document.cookie"));
  EXPECT_EQ("b=1", content::EvalJs(other, "document.cookie"));
}

// Clearing the cache leaves cookies alone; the name is matched without regard
// to case.
IN_PROC_BROWSER_TEST_F(AvoraQuickNavCommandBrowserTest,
                       ClearCacheKeepsCookies) {
  content::WebContents* page = OpenSiteWithCookie("a.test", "a=1");

  browser_view()->ShowAvoraQuickNavWithURL();
  RunCommandOn(page, u"clear cache");

  EXPECT_FALSE(quick_nav()->IsShowing());
  EXPECT_EQ("a=1", content::EvalJs(page, "document.cookie"));
}

// In a Space with an identity of its own, the command clears that Space's jar
// and nothing else: the same site's cookie in the default jar survives.
IN_PROC_BROWSER_TEST_F(AvoraQuickNavCommandBrowserTest,
                       ClearCookiesStaysInsideTheSpace) {
  ASSERT_TRUE(content::SetCookie(profile(), TestURL("a.test"), "default=1"));

  SpaceManager manager(profile()->GetPrefs());
  const std::string space_id = manager.CreateSpace("Work");
  WindowSpaceState* state = GetWindowSpaceState(browser());
  ASSERT_TRUE(state);
  state->SetActiveSpaceId(space_id);

  content::WebContents* page = OpenSiteWithCookie("a.test", "a=1");
  ASSERT_FALSE(PartitionOf(page).is_default());

  browser_view()->ShowAvoraQuickNavWithURL();
  RunCommandOn(page, u"Clear Cookies");

  EXPECT_EQ("", content::EvalJs(page, "document.cookie"));
  EXPECT_EQ("default=1", content::GetCookies(profile(), TestURL("a.test")));
}

// Cmd-T opens a blank tab before the overlay appears.  A command typed there
// is about the page underneath: it clears that page's site, returns to it, and
// drops the blank tab.
IN_PROC_BROWSER_TEST_F(AvoraQuickNavCommandBrowserTest,
                       CommandFromNewTabActsOnThePageItCovered) {
  content::WebContents* page = OpenSiteWithCookie("a.test", "a=1");
  const int tab_count = model()->count();

  // What Cmd-T does (BrowserNativeWidgetMac::ExecuteCommand): a new tab, then
  // the overlay over it.
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  browser_view()->ShowAvoraQuickNav();
  ASSERT_EQ(tab_count + 1, model()->count());
  ASSERT_NE(page, model()->GetActiveWebContents());
  ASSERT_TRUE(quick_nav()->IsShowing());

  RunCommandOn(page, u"Clear Cookies");

  EXPECT_EQ(tab_count, model()->count());
  EXPECT_EQ(page, model()->GetActiveWebContents());
  EXPECT_EQ("", content::EvalJs(page, "document.cookie"));
}

}  // namespace avora
