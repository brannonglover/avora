// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_mini_extensions_container.h"

#include <string>

#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/avora/avora_profile.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_storage_partition.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/extension_view_host.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/extensions/extensions_container.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_mini_toolbar_view.h"
#include "chrome/browser/ui/views/avora/avora_mini_window.h"
#include "chrome/browser/ui/views/extensions/extension_popup.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/window_feature_controller/window_feature_controller.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/browser/background_script_executor.h"
#include "extensions/common/extension.h"
#include "extensions/test/test_extension_dir.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/base_window.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace avora {

// Checkpoint 3: extensions in the mini window.
//
// The mini window is a popup browser, and a popup reports that it has no
// toolbar and builds no ExtensionsContainer.  chrome.action.openPopup() checks
// both, so an extension asking to show its popup while the mini window was
// active was simply told no -- which is how a click on 1Password's locked
// padlock in a mini window came to do nothing.  These tests cover the two
// pieces that answer that (the scoped kFeatureToolbar exception and
// AvoraMiniExtensionsContainer), that the popup lands against the mini
// window's own tab and window rather than the main window behind it, and that
// ordinary popup windows are left exactly as Chromium has them.
//
// What these tests do NOT establish: that 1Password itself unlocks and fills.
// A test extension calling openPopup() proves the API path is open, nothing
// more.  The unlock-and-fill workflow is in the manual checklist.
class AvoraMiniWindowExtensionsBrowserTest
    : public extensions::ExtensionBrowserTest {
 public:
  AvoraMiniWindowExtensionsBrowserTest() = default;

  void SetUpOnMainThread() override {
    extensions::ExtensionBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  void TearDownOnMainThread() override {
    CloseMiniWindow();
    base::RunLoop().RunUntilIdle();
    extensions::ExtensionBrowserTest::TearDownOnMainThread();
  }

 protected:
  GURL TestURL(const std::string& host, const std::string& path) {
    return embedded_test_server()->GetURL(host, path);
  }

  // An extension shaped like 1Password's relevant parts: an action with a
  // popup, and a service worker that can be told to open that popup -- which
  // is exactly what 1Password's background does when the user clicks its
  // locked padlock in the page.
  const extensions::Extension* LoadPopupExtension() {
    extension_dir_.WriteManifest(R"({
      "name": "Avora openPopup test",
      "manifest_version": 3,
      "version": "1.0",
      "background": {"service_worker": "worker.js"},
      "action": {"default_popup": "popup.html", "default_title": "Test"},
      "permissions": ["tabs"]
    })");
    extension_dir_.WriteFile(FILE_PATH_LITERAL("worker.js"), "");
    // The popup reports the tab and window it believes it is acting on.
    extension_dir_.WriteFile(FILE_PATH_LITERAL("popup.html"),
                             "<!doctype html><title>test popup</title>"
                             "<script src='popup.js'></script>");
    extension_dir_.WriteFile(
        FILE_PATH_LITERAL("popup.js"),
        "window.seen = new Promise(resolve => {"
        "  chrome.tabs.query({active: true, currentWindow: true}, tabs => {"
        "    resolve({tabId: tabs[0].id, windowId: tabs[0].windowId,"
        "             url: tabs[0].url});"
        "  });"
        "});");
    return LoadExtension(extension_dir_.UnpackedPath());
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

  AvoraMiniExtensionsContainer* MiniContainer() {
    BrowserWindowInterface* mini = GetMiniWindowBrowser();
    if (!mini) {
      return nullptr;
    }
    return static_cast<AvoraMiniExtensionsContainer*>(
        ExtensionsContainer::From(*mini));
  }

  // Asks the extension's own service worker to open its popup, the way
  // 1Password's background does.
  // Brings |target| to the front and gives the activation event a moment to
  // land.  Deliberately not an assertion: a browser test running in parallel
  // cannot demand that the window server activate its windows, which is why
  // the end-to-end helper below retries rather than relying on one attempt.
  bool ActivateAndWait(BrowserWindowInterface* target) {
    views::Widget* const widget =
        BrowserView::GetBrowserViewForBrowser(target)->GetWidget();
    target->GetWindow()->Activate();
    return base::test::RunUntil([&]() { return widget->IsActive(); });
  }

  // The real path 1Password takes: the extension's own service worker calls
  // chrome.action.openPopup().  Chromium refuses that call outright while the
  // target window is inactive, so this retries activation-plus-call rather
  // than failing on an activation race.  If the feature itself were broken no
  // number of retries would produce a popup, so a failure here is still a
  // failure of the thing under test.
  bool OpenPopupFromExtensionUntilShown(
      const extensions::Extension& extension,
      BrowserWindowInterface* target) {
    for (int attempt = 0; attempt < 5; ++attempt) {
      ActivateAndWait(target);
      if (!extensions::BackgroundScriptExecutor::ExecuteScriptAsync(
              profile(), extension.id(), "chrome.action.openPopup();")) {
        continue;
      }
      const bool shown = base::test::RunUntil([&]() {
        AvoraMiniExtensionsContainer* container = MiniContainer();
        return container && container->popup_host_for_testing() &&
               container->popup_host_for_testing()->host_contents();
      });
      if (shown) {
        return true;
      }
    }
    return false;
  }

  // Avora's own entry point -- the one Chromium calls once openPopup() has
  // passed its gate.  Deterministic: no window activation involved, so the
  // behaviours built on top of it can be tested without depending on focus.
  bool ShowPopupViaContainer(const extensions::Extension& extension) {
    AvoraMiniExtensionsContainer* container = MiniContainer();
    return container && container->ShowToolbarActionPopupForAPICall(
                            extension.id(), ShowPopupCallback());
  }

  bool WaitForPopup() {
    return base::test::RunUntil([&]() {
      AvoraMiniExtensionsContainer* container = MiniContainer();
      return container && container->popup_host_for_testing() &&
             container->popup_host_for_testing()->host_contents();
    });
  }

  std::string CreateAndActivateSpace(const std::string& name) {
    SpaceManager manager(profile()->GetPrefs());
    const std::string id = manager.CreateSpace(name);
    if (WindowSpaceState* state = GetWindowSpaceState(browser())) {
      state->SetActiveSpaceId(id);
    }
    return id;
  }

  extensions::TestExtensionDir extension_dir_;
};

// The window now says it has a toolbar -- scoped to the mini window, which is
// the whole point of the exception.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowExtensionsBrowserTest,
                       ToolbarExceptionIsScopedToTheMiniWindow) {
  BrowserWindowInterface* mini =
      OpenMiniWindow(TestURL("a.test", "/title1.html"));
  ASSERT_TRUE(mini);

  using WindowFeature = WindowFeatureController::WindowFeature;
  EXPECT_TRUE(WindowFeatureController::From(mini)->SupportsWindowFeature(
      WindowFeature::kFeatureToolbar));
  EXPECT_TRUE(IsMiniWindow(mini));

  // An ordinary popup keeps Chromium's answer, and gets no container.
  BrowserWindowCreateParams params(BrowserWindowInterface::TYPE_POPUP,
                                   profile(), /*from_user_gesture=*/true);
  BrowserWindowInterface* ordinary = CreateBrowserWindow(std::move(params));
  ASSERT_TRUE(ordinary);
  EXPECT_FALSE(IsMiniWindow(ordinary));
  EXPECT_FALSE(WindowFeatureController::From(ordinary)->SupportsWindowFeature(
      WindowFeature::kFeatureToolbar));
  EXPECT_EQ(nullptr, ExtensionsContainer::From(*ordinary));

  // And the main window is unchanged: a tabbed window has a toolbar and its
  // own standard container, which is not the mini window's.
  EXPECT_TRUE(WindowFeatureController::From(browser())->SupportsWindowFeature(
      WindowFeature::kFeatureToolbar));
  ExtensionsContainer* const main_container =
      ExtensionsContainer::From(*browser());
  ASSERT_TRUE(main_container);
  EXPECT_NE(main_container, ExtensionsContainer::From(*mini));

  CloseBrowserSynchronously(ordinary);
}

// The bug, end to end at the API level: an extension asking to show its popup
// while the mini window is active gets a popup.
//
// MANUAL_ because it cannot pass unattended.  chrome.action.openPopup()
// refuses any window that is not the active window, and a browser test running
// in the background cannot make the window server activate its windows -- the
// test then fails for want of focus rather than for anything it is meant to
// catch.  Run it deliberately, serially, on a machine whose display is free:
//
//   out/Default/avora_browser_tests \
//     --gtest_filter='*MANUAL_OpenPopupShowsAPopupInTheMiniWindow' \
//     --run-manual --test-launcher-jobs=1
//
// The gate it asserts first (toolbar reported, container present) is what the
// deterministic tests above cover unattended; this one adds the real API call
// on top.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowExtensionsBrowserTest,
                       MANUAL_OpenPopupShowsAPopupInTheMiniWindow) {
  const extensions::Extension* extension = LoadPopupExtension();
  ASSERT_TRUE(extension);

  BrowserWindowInterface* mini =
      OpenMiniWindow(TestURL("a.test", "/title1.html"));
  ASSERT_TRUE(mini);

  // Chromium's own gate, which is what used to refuse this outright: the
  // window must report a toolbar and carry an ExtensionsContainer.
  using WindowFeature = WindowFeatureController::WindowFeature;
  ASSERT_TRUE(WindowFeatureController::From(mini)->SupportsWindowFeature(
      WindowFeature::kFeatureToolbar));
  ASSERT_TRUE(MiniBrowserView()->IsToolbarVisible());
  ASSERT_TRUE(ExtensionsContainer::From(*mini));

  ASSERT_TRUE(OpenPopupFromExtensionUntilShown(*extension, mini));
  EXPECT_EQ(extension->id(),
            MiniContainer()->popup_host_for_testing()->extension_id());
}

// The popup must be about the mini window's page, in the mini window's Space
// -- not about whatever the main window happens to be showing.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowExtensionsBrowserTest,
                       PopupTargetsTheMiniWindowsTabAndSpace) {
  const std::string space = CreateAndActivateSpace("Work");
  const extensions::Extension* extension = LoadPopupExtension();
  ASSERT_TRUE(extension);

  const GURL mini_url = TestURL("a.test", "/title1.html");
  BrowserWindowInterface* mini = OpenMiniWindow(mini_url);
  ASSERT_TRUE(mini);

  // The window's identity, for comparison.
  ASSERT_EQ(space, MiniWindowController::Get()->space_id());
  const int mini_tab_id =
      sessions::SessionTabHelper::IdForTab(MiniContents()).id();
  const int mini_window_id = extensions::ExtensionTabUtil::GetWindowId(mini);
  const int main_tab_id = sessions::SessionTabHelper::IdForTab(
                              browser()->GetTabStripModel()
                                  ->GetActiveWebContents())
                              .id();

  ASSERT_TRUE(ShowPopupViaContainer(*extension));
  ASSERT_TRUE(WaitForPopup());

  // What the popup itself believes it is acting on.
  content::WebContents* popup_contents =
      MiniContainer()->popup_host_for_testing()->host_contents();
  ASSERT_TRUE(popup_contents);
  ASSERT_TRUE(content::WaitForLoadStop(popup_contents));
  // Each field read on its own: EvalJs awaits the promise and hands back a
  // plain value.
  EXPECT_EQ(mini_tab_id,
            content::EvalJs(popup_contents, "window.seen.then(s => s.tabId)")
                .ExtractInt());
  EXPECT_EQ(mini_window_id,
            content::EvalJs(popup_contents, "window.seen.then(s => s.windowId)")
                .ExtractInt());
  EXPECT_NE(main_tab_id,
            content::EvalJs(popup_contents, "window.seen.then(s => s.tabId)")
                .ExtractInt());

  // And the tab it named is still the one pinned to the Space's partition.
  SpaceManager manager(profile()->GetPrefs());
  const Space* const work = manager.GetSpaceById(space);
  ASSERT_TRUE(work);
  BrowserProfileStore store(profile()->GetPrefs());
  const BrowserProfile* const identity = store.GetProfileById(work->profile_id);
  ASSERT_TRUE(identity);
  EXPECT_EQ(GetPartitionConfigForProfile(profile(), *identity),
            MiniContents()->GetPrimaryMainFrame()->GetStoragePartition()
                ->GetConfig());
}

// The popup hangs from the mini window's own chrome, so it appears over the
// mini window rather than over the main window behind it.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowExtensionsBrowserTest,
                       PopupAnchorsToTheMiniWindow) {
  const extensions::Extension* extension = LoadPopupExtension();
  ASSERT_TRUE(extension);

  BrowserWindowInterface* mini =
      OpenMiniWindow(TestURL("a.test", "/title1.html"));
  ASSERT_TRUE(mini);
  // The toolbar carries an icon for the extension, which is the anchor.
  ASSERT_TRUE(MiniToolbar()->ExtensionAnchor(extension->id()));

  ASSERT_TRUE(ShowPopupViaContainer(*extension));
  ASSERT_TRUE(WaitForPopup());

  ExtensionPopup* const popup = ExtensionPopup::last_popup_for_testing();
  ASSERT_TRUE(popup);
  ASSERT_TRUE(popup->GetAnchorView());
  views::Widget* const anchor_widget = popup->GetAnchorView()->GetWidget();
  EXPECT_EQ(MiniBrowserView()->GetWidget(), anchor_widget);
  EXPECT_NE(BrowserView::GetBrowserViewForBrowser(browser())->GetWidget(),
            anchor_widget);
  // The anchor is the extension's own icon in the mini toolbar.
  EXPECT_EQ(MiniToolbar()->ExtensionAnchor(extension->id()),
            popup->GetAnchorView());
}

// Dismissal and reopening: the popup goes away when asked, and opening it
// again works rather than being refused because the first one is still around.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowExtensionsBrowserTest,
                       PopupDismissesAndReopens) {
  const extensions::Extension* extension = LoadPopupExtension();
  ASSERT_TRUE(extension);

  BrowserWindowInterface* mini =
      OpenMiniWindow(TestURL("a.test", "/title1.html"));
  ASSERT_TRUE(mini);
  ASSERT_TRUE(ShowPopupViaContainer(*extension));
  ASSERT_TRUE(WaitForPopup());

  // What every unchecked caller in Chromium does to a window's container.
  MiniContainer()->HideActivePopup();
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return MiniContainer()->popup_host_for_testing() == nullptr; }));

  // Safe to ask again with nothing showing -- this is the call
  // ExtensionContextMenuModel makes without a null check.
  MiniContainer()->HideActivePopup();
  MiniContainer()->CloseExtensionsMenuIfOpen();

  // And it opens again afterwards rather than being refused because the first
  // one is somehow still around.
  EXPECT_TRUE(ShowPopupViaContainer(*extension));
  EXPECT_TRUE(WaitForPopup());
}

// Closing the window with a popup still up must tear both down in order.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowExtensionsBrowserTest,
                       ClosingTheMiniWindowWithAPopupOpenIsClean) {
  const extensions::Extension* extension = LoadPopupExtension();
  ASSERT_TRUE(extension);

  BrowserWindowInterface* mini =
      OpenMiniWindow(TestURL("a.test", "/title1.html"));
  ASSERT_TRUE(mini);
  ASSERT_TRUE(ShowPopupViaContainer(*extension));
  ASSERT_TRUE(WaitForPopup());

  CloseMiniWindow();
  EXPECT_FALSE(IsMiniWindowShowing());
  base::RunLoop().RunUntilIdle();

  // The container went with the window, and nothing is left pointing at a
  // popup host whose widget is gone.
  EXPECT_EQ(nullptr, GetMiniWindowBrowser());

  // The main window is untouched and still has its own container.
  EXPECT_TRUE(ExtensionsContainer::From(*browser()));
  EXPECT_FALSE(browser()->IsDeleteScheduled());
}

// With both windows open, the extension's popup follows the active window --
// the mini window when it is in front, the main window when it is not.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowExtensionsBrowserTest,
                       PopupFollowsTheActiveWindow) {
  const extensions::Extension* extension = LoadPopupExtension();
  ASSERT_TRUE(extension);

  BrowserWindowInterface* mini =
      OpenMiniWindow(TestURL("a.test", "/title1.html"));
  ASSERT_TRUE(mini);

  // Each window has its own container, and the mini window's is the only one
  // that can put a popup in the mini window.
  ExtensionsContainer* const main_container =
      ExtensionsContainer::From(*browser());
  ExtensionsContainer* const mini_container = ExtensionsContainer::From(*mini);
  ASSERT_TRUE(main_container);
  ASSERT_TRUE(mini_container);
  EXPECT_NE(main_container, mini_container);

  // Asking the main window's container to show the popup leaves the mini
  // window's empty -- the two cannot cross.
  main_container->ShowToolbarActionPopupForAPICall(extension->id(),
                                                   ShowPopupCallback());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(nullptr, MiniContainer()->popup_host_for_testing());

  // And the mini window's container puts it in the mini window.
  ASSERT_TRUE(ShowPopupViaContainer(*extension));
  EXPECT_TRUE(WaitForPopup());
}

}  // namespace avora
