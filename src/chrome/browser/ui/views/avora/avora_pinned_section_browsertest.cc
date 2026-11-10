// Copyright 2026 Avora. All rights reserved.

#include <string>

#include "base/test/run_until.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_pinned_section_view.h"
#include "chrome/browser/ui/views/avora/avora_window_theme.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/canvas.h"
#include "ui/base/window_open_disposition.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace avora {

// A pinned tab that is the window's active tab has to look it.  Pinned rows
// used to paint only a hover fill, so the active pin was indistinguishable
// from the others -- unlike a daily tab, which wears tab_active.
class AvoraPinnedSectionBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  GURL TestURL(const std::string& host) {
    return embedded_test_server()->GetURL(host, "/title1.html");
  }

  static PinnedTabRow* FindRow(views::View* root,
                               content::WebContents* contents) {
    if (auto* row = views::AsViewClass<PinnedTabRow>(root)) {
      return row->web_contents() == contents ? row : nullptr;
    }
    for (views::View* child : root->children()) {
      if (PinnedTabRow* row = FindRow(child, contents)) {
        return row;
      }
    }
    return nullptr;
  }

  PinnedTabRow* RowFor(content::WebContents* contents) {
    return FindRow(BrowserView::GetBrowserViewForBrowser(browser()), contents);
  }
};

IN_PROC_BROWSER_TEST_F(AvoraPinnedSectionBrowserTest, ActivePinIsMarked) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), TestURL("a.test")));
  content::WebContents* pinned = model->GetActiveWebContents();
  model->SetTabPinned(model->active_index(), true);

  ASSERT_TRUE(base::test::RunUntil([&]() { return RowFor(pinned); }));
  EXPECT_TRUE(RowFor(pinned)->is_active());

  // Activating a daily tab takes the mark off the pin...
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), TestURL("b.test"), WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  ASSERT_NE(pinned, model->GetActiveWebContents());
  ASSERT_TRUE(RowFor(pinned));
  EXPECT_FALSE(RowFor(pinned)->is_active());

  // ...and going back to the pin puts it back.
  model->ActivateTabAt(model->GetIndexOfWebContents(pinned));
  ASSERT_TRUE(RowFor(pinned));
  EXPECT_TRUE(RowFor(pinned)->is_active());
}

// The active pin wears a daily tab's pill: tab_active, inset from the
// sidebar's edges by the strip's horizontal padding, as an active TabView is.
IN_PROC_BROWSER_TEST_F(AvoraPinnedSectionBrowserTest,
                       ActivePinIsPaintedLikeADailyTab) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), TestURL("a.test")));
  content::WebContents* pinned = model->GetActiveWebContents();
  model->SetTabPinned(model->active_index(), true);
  // Laid out, not merely built: the row has no width until the next layout.
  ASSERT_TRUE(base::test::RunUntil([&]() {
    PinnedTabRow* candidate = RowFor(pinned);
    return candidate && candidate->width() > 40;
  }));
  PinnedTabRow* row = RowFor(pinned);
  ASSERT_TRUE(row->is_active());

  gfx::Canvas canvas(row->size(), 1.0f, /*is_opaque=*/false);
  row->OnPaintBackground(&canvas);
  const SkBitmap bitmap = canvas.GetBitmap();
  const int mid_y = row->height() / 2;
  const int inset =
      GetLayoutConstant(LayoutConstant::kVerticalTabStripHorizontalPadding);

  EXPECT_EQ(WindowTheme::PaletteFor(row).tab_active,
            bitmap.getColor(row->width() / 2, mid_y));
  // Nothing painted in the strip's padding, on either side.
  EXPECT_EQ(SK_ColorTRANSPARENT, bitmap.getColor(inset - 1, mid_y));
  EXPECT_EQ(SK_ColorTRANSPARENT,
            bitmap.getColor(row->width() - inset, mid_y));
}

}  // namespace avora
