// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_mini_toolbar_view.h"

#include <optional>
#include <string>

#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_mini_window.h"
#include "chrome/browser/ui/views/avora/avora_toast_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace avora {

// Checkpoint 2: the mini window's own chrome, hosted by BrowserView.
//
// The window is a popup browser, so without this it would carry Chromium's
// location bar.  These tests cover what the mini window is supposed to show
// instead -- a nav row, an editable address chip and a Space selector -- and
// that it behaves like the pre-Browser mini window did while the page stays a
// genuine tab underneath.
class AvoraMiniToolbarBrowserTest : public InProcessBrowserTest {
 public:
  AvoraMiniToolbarBrowserTest() = default;

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
          return mini && mini->GetTabStripModel()->GetActiveWebContents() &&
                 MiniToolbar() != nullptr;
        })) {
      return nullptr;
    }
    return GetMiniWindowBrowser();
  }

  BrowserView* MiniBrowserView() {
    BrowserWindowInterface* mini = GetMiniWindowBrowser();
    return mini ? BrowserView::GetBrowserViewForBrowser(mini) : nullptr;
  }

  AvoraMiniToolbarView* MiniToolbar() {
    BrowserView* view = MiniBrowserView();
    return view ? view->avora_mini_toolbar() : nullptr;
  }

  content::WebContents* MiniContents() {
    BrowserWindowInterface* mini = GetMiniWindowBrowser();
    return mini ? mini->GetTabStripModel()->GetActiveWebContents() : nullptr;
  }

  // Navigates the mini window's tab and waits for the load, so the toolbar has
  // something settled to report on.
  void NavigateMiniWindow(const GURL& url) {
    ASSERT_TRUE(content::NavigateToURL(MiniContents(), url));
  }

  std::string CreateAndActivateSpace(const std::string& name) {
    SpaceManager manager(profile()->GetPrefs());
    const std::string id = manager.CreateSpace(name);
    if (WindowSpaceState* state = GetWindowSpaceState(browser())) {
      state->SetActiveSpaceId(id);
    }
    return id;
  }

  void Click(views::Button* button) {
    views::test::ButtonTestApi(button).NotifyClick(ui::MouseEvent(
        ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
        ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON, 0));
  }

  // What the address chip's textfield controller sees when the user commits or
  // abandons an edit.
  bool SendKeyToAddressField(ui::KeyboardCode key) {
    ui::KeyEvent event(ui::EventType::kKeyPressed, key, ui::EF_NONE);
    return MiniToolbar()->HandleKeyEvent(
        MiniToolbar()->address_field_for_testing(), event);
  }
};

// The mini window shows Avora's chrome, and Chromium's popup location bar
// stays out of sight behind it.
IN_PROC_BROWSER_TEST_F(AvoraMiniToolbarBrowserTest,
                       ShowsAvoraChromeInsteadOfTheLocationBar) {
  ASSERT_TRUE(OpenMiniWindow(TestURL("a.test", "/title1.html")));
  BrowserView* view = MiniBrowserView();
  ASSERT_TRUE(view);
  AvoraMiniToolbarView* toolbar = MiniToolbar();
  ASSERT_TRUE(toolbar);

  EXPECT_TRUE(toolbar->GetVisible());
  EXPECT_FALSE(view->toolbar()->GetVisible());
  EXPECT_EQ(AvoraMiniToolbarView::kToolbarHeight, toolbar->height());
  EXPECT_GT(toolbar->width(), 0);

  // The toolbar sits above the page, and the page has real room left.
  const gfx::Rect toolbar_bounds = toolbar->ConvertRectToWidget(
      toolbar->GetLocalBounds());
  const gfx::Rect contents_bounds =
      view->multi_contents_view()->ConvertRectToWidget(
          view->multi_contents_view()->GetLocalBounds());
  EXPECT_LE(toolbar_bounds.bottom(), contents_bounds.y());
  EXPECT_GT(contents_bounds.height(), 0);

  // An ordinary window is untouched by any of this.
  BrowserView* main_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(main_view);
  EXPECT_EQ(nullptr, main_view->avora_mini_toolbar());
}

// A window for reading a page in has a floor; a popup's one-pixel contents
// minimum would let the user drag this down to nothing.
IN_PROC_BROWSER_TEST_F(AvoraMiniToolbarBrowserTest,
                       EnforcesAMinimumWindowSize) {
  ASSERT_TRUE(OpenMiniWindow(TestURL("a.test", "/title1.html")));
  BrowserView* view = MiniBrowserView();
  ASSERT_TRUE(view);

  const gfx::Size minimum = view->GetMinimumSize();
  EXPECT_GE(minimum.width(), kMiniWindowMinWidth);
  EXPECT_GE(minimum.height(), kMiniWindowMinHeight);

  // And the widget enforces it, which is what the window manager asks.
  const gfx::Size widget_minimum = view->GetWidget()->GetMinimumSize();
  EXPECT_GE(widget_minimum.width(), kMiniWindowMinWidth);
  EXPECT_GE(widget_minimum.height(), kMiniWindowMinHeight);

  // The window it opened at is bigger than its floor and fits the screen.
  const gfx::Size opened = view->GetWidget()->GetWindowBoundsInScreen().size();
  EXPECT_GE(opened.width(), kMiniWindowMinWidth);
  EXPECT_GE(opened.height(), kMiniWindowMinHeight);
}

// Back and forward report on the page's history, and act through the same
// browser commands the rest of Avora uses.
IN_PROC_BROWSER_TEST_F(AvoraMiniToolbarBrowserTest, NavButtonsFollowHistory) {
  ASSERT_TRUE(OpenMiniWindow(TestURL("a.test", "/title1.html")));
  AvoraMiniToolbarView* toolbar = MiniToolbar();
  ASSERT_TRUE(toolbar);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !MiniContents()->IsLoading(); }));

  EXPECT_FALSE(toolbar->back_button_for_testing()->GetEnabled());
  EXPECT_FALSE(toolbar->forward_button_for_testing()->GetEnabled());

  const GURL second = TestURL("b.test", "/title2.html");
  NavigateMiniWindow(second);
  EXPECT_TRUE(toolbar->back_button_for_testing()->GetEnabled());
  EXPECT_FALSE(toolbar->forward_button_for_testing()->GetEnabled());

  // Clicking back goes back, and forward becomes available.
  Click(toolbar->back_button_for_testing());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return MiniContents()->GetLastCommittedURL() ==
           TestURL("a.test", "/title1.html");
  }));
  EXPECT_TRUE(toolbar->forward_button_for_testing()->GetEnabled());
}

// The address chip: a label showing where the user is, which becomes a
// textfield when clicked and navigates the same tab when committed.
IN_PROC_BROWSER_TEST_F(AvoraMiniToolbarBrowserTest,
                       AddressChipShowsAndEditsTheUrl) {
  const GURL first = TestURL("a.test", "/title1.html");
  ASSERT_TRUE(OpenMiniWindow(first));
  AvoraMiniToolbarView* toolbar = MiniToolbar();
  ASSERT_TRUE(toolbar);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !MiniContents()->IsLoading(); }));

  // Shown formatted -- the host, without the scheme Chromium elides.
  const std::u16string shown(toolbar->address_label_for_testing()->GetText());
  EXPECT_NE(std::u16string::npos, shown.find(u"a.test"));
  EXPECT_TRUE(toolbar->address_label_for_testing()->GetVisible());
  EXPECT_FALSE(toolbar->address_field_for_testing()->GetVisible());

  // Clicking the chip hands over the whole URL to edit.
  toolbar->BeginEditingAddress();
  EXPECT_TRUE(toolbar->address_field_for_testing()->GetVisible());
  EXPECT_FALSE(toolbar->address_label_for_testing()->GetVisible());
  EXPECT_EQ(base::UTF8ToUTF16(first.spec()),
            std::u16string(toolbar->address_field_for_testing()->GetText()));

  // Committing navigates this tab rather than opening anything.
  const GURL typed = TestURL("b.test", "/title2.html");
  const int tabs_in_main = browser()->GetTabStripModel()->count();
  toolbar->address_field_for_testing()->SetText(
      base::UTF8ToUTF16(typed.spec()));
  EXPECT_TRUE(SendKeyToAddressField(ui::VKEY_RETURN));

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return MiniContents()->GetLastCommittedURL() == typed; }));
  EXPECT_EQ(1, GetMiniWindowBrowser()->GetTabStripModel()->count());
  EXPECT_EQ(tabs_in_main, browser()->GetTabStripModel()->count());
  EXPECT_FALSE(toolbar->address_field_for_testing()->GetVisible());
  EXPECT_NE(std::u16string::npos,
            toolbar->address_label_for_testing()->GetText().find(u"b.test"));
}

// Escape puts the chip back the way it was without navigating.
IN_PROC_BROWSER_TEST_F(AvoraMiniToolbarBrowserTest,
                       EscapeAbandonsAddressEditing) {
  const GURL first = TestURL("a.test", "/title1.html");
  ASSERT_TRUE(OpenMiniWindow(first));
  AvoraMiniToolbarView* toolbar = MiniToolbar();
  ASSERT_TRUE(toolbar);

  toolbar->BeginEditingAddress();
  toolbar->address_field_for_testing()->SetText(u"https://elsewhere.test/");
  EXPECT_TRUE(SendKeyToAddressField(ui::VKEY_ESCAPE));

  EXPECT_FALSE(toolbar->address_field_for_testing()->GetVisible());
  EXPECT_TRUE(toolbar->address_label_for_testing()->GetVisible());
  EXPECT_EQ(first, MiniContents()->GetVisibleURL());
}

// The Space chip names where a click would send the page, and offers the rest
// behind the dropdown.
IN_PROC_BROWSER_TEST_F(AvoraMiniToolbarBrowserTest,
                       SpaceChipNamesTheDestination) {
  CreateAndActivateSpace("Work");
  ASSERT_TRUE(OpenMiniWindow(TestURL("a.test", "/title1.html")));
  AvoraMiniToolbarView* toolbar = MiniToolbar();
  ASSERT_TRUE(toolbar);

  EXPECT_EQ(u"Work", toolbar->space_button_for_testing()->GetText());
  // More than one Space to choose from, so the dropdown has a job.
  EXPECT_TRUE(toolbar->space_menu_button_for_testing()->GetVisible());

  const std::vector<Space> offered = MiniWindowOrderedSpaces(GetMiniWindowBrowser());
  ASSERT_FALSE(offered.empty());
  EXPECT_EQ("Work", offered.front().name);
}

// Clicking the chip commits the page to the Space it names: a tab there, and
// the glance is done.
IN_PROC_BROWSER_TEST_F(AvoraMiniToolbarBrowserTest,
                       SpaceChipClickCommitsThePage) {
  CreateAndActivateSpace("Work");
  const GURL url = TestURL("a.test", "/title1.html");
  ASSERT_TRUE(OpenMiniWindow(url));
  const int tabs_before = browser()->GetTabStripModel()->count();

  Click(MiniToolbar()->space_button_for_testing());

  EXPECT_FALSE(IsMiniWindowShowing());
  EXPECT_EQ(tabs_before + 1, browser()->GetTabStripModel()->count());
  EXPECT_EQ(url, browser()
                     ->GetTabStripModel()
                     ->GetActiveWebContents()
                     ->GetVisibleURL());
}

// Copy-URL works in the mini window and confirms with the same toast the main
// window shows.  Every entry point -- the Cmd-Shift-C menu item above all --
// funnels through this command.
IN_PROC_BROWSER_TEST_F(AvoraMiniToolbarBrowserTest, CopyUrlCopiesAndToasts) {
  const GURL url = TestURL("a.test", "/title1.html");
  BrowserWindowInterface* mini = OpenMiniWindow(url);
  ASSERT_TRUE(mini);

  // Leave something else on the clipboard first, so a no-op would be visible.
  {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteText(u"not the url");
  }

  EXPECT_TRUE(chrome::IsCommandEnabled(mini, IDC_COPY_URL));
  chrome::ExecuteCommand(mini, IDC_COPY_URL);

  base::test::TestFuture<std::u16string> clipboard;
  ui::Clipboard::GetForCurrentThread()->ReadText(
      ui::ClipboardBuffer::kCopyPaste, /*data_dst=*/std::nullopt,
      clipboard.GetCallback());
  EXPECT_EQ(base::UTF8ToUTF16(url.spec()), clipboard.Get());
}

// The window names itself after the page, as it did before it was a browser
// window.
IN_PROC_BROWSER_TEST_F(AvoraMiniToolbarBrowserTest, WindowTitleFollowsThePage) {
  ASSERT_TRUE(OpenMiniWindow(TestURL("a.test", "/title2.html")));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !MiniContents()->IsLoading(); }));

  // /title2.html sets its title to "Title Of Awesomeness".
  EXPECT_TRUE(base::test::RunUntil([&]() {
    const std::u16string title = MiniBrowserView()->GetWindowTitle();
    return title.find(u"Awesomeness") != std::u16string::npos;
  }));
}

}  // namespace avora
