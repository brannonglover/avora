// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_quick_nav_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "ui/base/page_transition_types.h"
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

}  // namespace avora
