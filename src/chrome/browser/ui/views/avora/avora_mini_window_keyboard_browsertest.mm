// Copyright 2026 Avora. All rights reserved.

#import <Cocoa/Cocoa.h>

#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_mini_toolbar_view.h"
#include "chrome/browser/ui/views/avora/avora_mini_window.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#import "ui/events/test/cocoa_test_event_utils.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace avora {

// The mini window's keyboard shortcuts, pressed the way a person presses them:
// a real NSEvent handed to [NSApp sendEvent:] while the mini window is key.
// That takes the whole AppKit route -- the page gets first refusal, the event
// comes back unhandled and is redispatched to the main menu -- which a
// chrome::ExecuteCommand call in a test skips entirely.
class AvoraMiniWindowKeyboardBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  // Opens a mini window on |url|, waits for the page to load, makes the
  // window key and gives the page focus -- where a mini window's focus sits
  // when the user starts typing shortcuts.
  BrowserWindowInterface* OpenFocusedMiniWindow(const GURL& url) {
    ShowMiniWindow(browser()->GetProfile(), url);
    if (!base::test::RunUntil([&]() {
          BrowserWindowInterface* mini = GetMiniWindowBrowser();
          return mini && Contents(mini) && !Contents(mini)->IsLoading() &&
                 Contents(mini)->GetLastCommittedURL() == url;
        })) {
      return nullptr;
    }
    BrowserWindowInterface* mini = GetMiniWindowBrowser();
    if (!ui_test_utils::BringBrowserWindowToFront(mini)) {
      return nullptr;
    }
    Contents(mini)->Focus();
    return mini;
  }

  static content::WebContents* Contents(BrowserWindowInterface* browser) {
    return browser->GetTabStripModel()->GetActiveWebContents();
  }

  static AvoraMiniToolbarView* Toolbar(BrowserWindowInterface* browser) {
    BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser);
    return view ? view->avora_mini_toolbar() : nullptr;
  }

  static NSWindow* NativeWindow(BrowserWindowInterface* browser) {
    return BrowserView::GetBrowserViewForBrowser(browser)
        ->GetWidget()
        ->GetNativeWindow()
        .GetNativeNSWindow();
  }

  // Presses and releases Cmd+|key| in |browser|'s window.
  static void PressCommandKey(BrowserWindowInterface* browser,
                              ui::KeyboardCode key) {
    NSWindow* window = NativeWindow(browser);
    [NSApp sendEvent:cocoa_test_event_utils::SynthesizeKeyEvent(
                         window, /*keyDown=*/true, key,
                         NSEventModifierFlagCommand)];
    [NSApp sendEvent:cocoa_test_event_utils::SynthesizeKeyEvent(
                         window, /*keyDown=*/false, key,
                         NSEventModifierFlagCommand)];
  }
};

// Cmd-L opens the address chip for editing, with the page's URL in it.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowKeyboardBrowserTest,
                       CommandLEditsTheAddress) {
  const GURL url = embedded_test_server()->GetURL("a.test", "/title1.html");
  BrowserWindowInterface* mini = OpenFocusedMiniWindow(url);
  ASSERT_TRUE(mini);
  AvoraMiniToolbarView* toolbar = Toolbar(mini);
  ASSERT_TRUE(toolbar);
  ASSERT_FALSE(toolbar->address_field_for_testing()->GetVisible());

  PressCommandKey(mini, ui::VKEY_L);

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return toolbar->address_field_for_testing()->GetVisible(); }));
  EXPECT_FALSE(toolbar->address_label_for_testing()->GetVisible());
  EXPECT_TRUE(toolbar->address_field_for_testing()->HasFocus());
  EXPECT_EQ(base::UTF8ToUTF16(url.spec()),
            std::u16string(toolbar->address_field_for_testing()->GetText()));
}

// Cmd-R reloads the mini window's page -- and only that page.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowKeyboardBrowserTest,
                       CommandRReloadsThePage) {
  const GURL url = embedded_test_server()->GetURL("a.test", "/title1.html");
  BrowserWindowInterface* mini = OpenFocusedMiniWindow(url);
  ASSERT_TRUE(mini);
  content::WebContents* contents = Contents(mini);
  const int entries_before = contents->GetController().GetEntryCount();

  content::TestNavigationObserver reload_observer(contents);
  PressCommandKey(mini, ui::VKEY_R);
  reload_observer.Wait();

  EXPECT_EQ(url, reload_observer.last_navigation_url());
  EXPECT_TRUE(reload_observer.last_navigation_succeeded());
  // A reload, not a new navigation: history did not grow.
  EXPECT_EQ(entries_before, contents->GetController().GetEntryCount());
}

// Cmd-R still reloads while the address chip has focus, as it does from the
// main window's address bar.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowKeyboardBrowserTest,
                       CommandRReloadsWhileEditingTheAddress) {
  const GURL url = embedded_test_server()->GetURL("a.test", "/title1.html");
  BrowserWindowInterface* mini = OpenFocusedMiniWindow(url);
  ASSERT_TRUE(mini);
  AvoraMiniToolbarView* toolbar = Toolbar(mini);
  ASSERT_TRUE(toolbar);
  toolbar->BeginEditingAddress();
  ASSERT_TRUE(toolbar->address_field_for_testing()->HasFocus());

  content::TestNavigationObserver reload_observer(Contents(mini));
  PressCommandKey(mini, ui::VKEY_R);
  reload_observer.Wait();

  EXPECT_EQ(url, reload_observer.last_navigation_url());
}

}  // namespace avora
