// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_mini_window.h"

#include "base/run_loop.h"
#include "chrome/browser/ui/browser.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/web_modal/web_contents_modal_dialog_host.h"
#include "components/web_modal/web_contents_modal_dialog_manager.h"
#include "components/web_modal/web_contents_modal_dialog_manager_delegate.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace avora {

namespace {

// A web-modal dialog with nothing in it.  What matters is where it is hosted,
// not what it contains.  Built the same way constrained_window builds its
// own, and handed to the same unchecked entry point ExternalProtocolDialog
// uses -- ShowWebModalDialogViews, not the ShowWebModal wrapper, which checks
// for the delegate and quietly falls back to a browser-modal dialog.
views::WidgetDelegate* MakeChildModalDialog() {
  return views::BubbleDialogModelHost::CreateModal(
             ui::DialogModel::Builder().SetTitle(u"Test").Build(),
             ui::mojom::ModalType::kChild)
      .release();
}

}  // namespace

// The mini window's page never enters a TabStripModel, so none of the wiring
// Browser does for a tab runs for it.  One piece of that wiring is the
// WebContentsModalDialogManager's delegate, and the external protocol prompt
// -- the dialog a page gets when it hands off to another app, which is how a
// Slack login returns to the Slack app -- dereferences it without checking.
// A mini window that does not host its own web-modal dialogs therefore takes
// the browser process down the first time a link in it leaves the web.
class AvoraMiniWindowBrowserTest : public InProcessBrowserTest {
 public:
  AvoraMiniWindowBrowserTest() = default;

  void TearDownOnMainThread() override {
    CloseMiniWindow();
    InProcessBrowserTest::TearDownOnMainThread();
  }

 protected:
  content::WebContents* OpenMiniWindow() {
    ShowMiniWindow(browser()->GetProfile(), GURL("about:blank"));
    MiniWindowView* view = GetMiniWindowViewForTesting();
    return view ? view->web_contents_for_testing() : nullptr;
  }
};

// The invariant the external protocol prompt relies on.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowBrowserTest, PageHasAModalDialogHost) {
  content::WebContents* contents = OpenMiniWindow();
  ASSERT_TRUE(contents);

  auto* manager =
      web_modal::WebContentsModalDialogManager::FromWebContents(contents);
  // Attaching the tab helpers creates the manager; upstream treats a missing
  // one as "this page cannot show dialogs" and skips the prompt, but a
  // present one as a promise that a delegate came with it.
  ASSERT_TRUE(manager);
  ASSERT_TRUE(manager->delegate());

  web_modal::WebContentsModalDialogHost* host =
      manager->delegate()->GetWebContentsModalDialogHost(contents);
  ASSERT_TRUE(host);
  EXPECT_TRUE(host->GetHostView());
  // A dialog may not grow past the page, so the page has to have a size.
  EXPECT_FALSE(host->GetMaximumDialogSize().IsEmpty());
}

// The call that crashed: constrained_window reaches through the manager's
// delegate to find the host with no null check of its own.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowBrowserTest, ShowsAWebModalDialog) {
  content::WebContents* contents = OpenMiniWindow();
  ASSERT_TRUE(contents);

  views::Widget* widget = constrained_window::ShowWebModalDialogViews(
      MakeChildModalDialog(), contents);
  ASSERT_TRUE(widget);
  EXPECT_TRUE(widget->IsVisible());

  auto* manager =
      web_modal::WebContentsModalDialogManager::FromWebContents(contents);
  ASSERT_TRUE(manager);
  EXPECT_TRUE(manager->IsDialogActive());

  // Positioned over the page, within it.
  const gfx::Size size = widget->GetWindowBoundsInScreen().size();
  web_modal::WebContentsModalDialogHost* host =
      manager->delegate()->GetWebContentsModalDialogHost(contents);
  const gfx::Point position = host->GetDialogPosition(size);
  EXPECT_GE(position.x(), 0);
  EXPECT_GT(position.y(), 0);
}

// Closing the window while a dialog is up must not leave the dialog holding a
// pointer to a host that is gone.
IN_PROC_BROWSER_TEST_F(AvoraMiniWindowBrowserTest, ClosesWithADialogUp) {
  content::WebContents* contents = OpenMiniWindow();
  ASSERT_TRUE(contents);

  ASSERT_TRUE(constrained_window::ShowWebModalDialogViews(
      MakeChildModalDialog(), contents));

  CloseMiniWindow();
  EXPECT_FALSE(IsMiniWindowShowing());
  // The mini window tears down on the next turn of the loop.
  base::RunLoop().RunUntilIdle();
}

}  // namespace avora
