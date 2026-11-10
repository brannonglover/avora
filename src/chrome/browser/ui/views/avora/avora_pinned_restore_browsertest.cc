// Copyright 2026 Avora. All rights reserved.

#include <string>

#include "base/functional/bind.h"
#include "base/test/run_until.h"
#include "chrome/browser/avora/avora_pinned_items.h"
#include "chrome/browser/avora/avora_sidebar_item.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_tab_marker.h"
#include "chrome/browser/ui/views/avora/avora_pinned_section_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace avora {

namespace {

template <typename T>
T* FindView(views::View* root,
            const base::RepeatingCallback<bool(T*)>& match) {
  if (auto* view = views::AsViewClass<T>(root); view && match.Run(view)) {
    return view;
  }
  for (views::View* child : root->children()) {
    if (T* found = FindView<T>(child, match)) {
      return found;
    }
  }
  return nullptr;
}

}  // namespace

// A pinned item's tab is an ordinary strip tab that only a marker on its
// WebContents keeps out of the daily list.  The marker does not survive a
// restart, so it has to be put back on the restored tab -- including when the
// tab has since browsed off the pin's own URL, which is where a URL match
// can't find it and the tab used to come back as a daily tab beside the pin.
class AvoraPinnedRestoreBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    SessionStartupPref::SetStartupPref(
        browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  }

 protected:
  GURL TestURL(const std::string& host) {
    return embedded_test_server()->GetURL(host, "/title1.html");
  }

  TabStripModel* model() { return browser()->GetTabStripModel(); }

  views::View* Root() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

  AvoraPinnedSectionView* Section() {
    return FindView<AvoraPinnedSectionView>(
        Root(), base::BindRepeating([](AvoraPinnedSectionView*) {
          return true;
        }));
  }

  PinnedTabRow* LiveRow(content::WebContents* contents) {
    return FindView<PinnedTabRow>(
        Root(), base::BindRepeating(
                    [](content::WebContents* wc, PinnedTabRow* row) {
                      return row->web_contents() == wc;
                    },
                    contents));
  }

  // The row a pin shows while it has no tab in this window.  Matched by host,
  // as PinnedItemOnHost() is.
  FolderTabRow* ClosedRow(const std::string& host) {
    return FindView<FolderTabRow>(
        Root(), base::BindRepeating(
                    [](const std::string& want, FolderTabRow* row) {
                      return GURL(row->url()).host() == want;
                    },
                    host));
  }

  content::WebContents* TabForItem(const std::string& item_id) {
    for (int i = 0; i < model()->count(); ++i) {
      content::WebContents* contents = model()->GetWebContentsAt(i);
      if (GetPinnedItemIdForTab(contents) == item_id) {
        return contents;
      }
    }
    return nullptr;
  }

  content::WebContents* TabOnHost(const std::string& host) {
    for (int i = 0; i < model()->count(); ++i) {
      content::WebContents* contents = model()->GetWebContentsAt(i);
      if (contents->GetVisibleURL().host() == host) {
        return contents;
      }
    }
    return nullptr;
  }

  // The kPinned item on |host|.  Matched by host because the test server's
  // port changes between the PRE_ run and the restart.
  std::string PinnedItemOnHost(const std::string& host) {
    SidebarItemStore store(browser()->GetProfile()->GetPrefs());
    for (const SidebarItem& item : store.GetAllItems()) {
      if (item.type == SidebarItemType::kPinned &&
          GURL(item.url).host() == host) {
        return item.id;
      }
    }
    return std::string();
  }

  bool TabStateRecordsPinnedItem(const std::string& item_id) {
    SidebarItemStore store(browser()->GetProfile()->GetPrefs());
    for (const SidebarItem& item : store.GetAllItems()) {
      if (item.type == SidebarItemType::kTabState &&
          item.pinned_item_id == item_id) {
        return true;
      }
    }
    return false;
  }
};

// Pins a.test, opens it from its row, and browses the tab on to c.test.
IN_PROC_BROWSER_TEST_F(AvoraPinnedRestoreBrowserTest,
                       PRE_PinnedTabOffItsTargetStaysPinned) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), TestURL("b.test")));

  const GURL pin_url = TestURL("a.test");
  ASSERT_TRUE(Section());
  const std::string item_id =
      Section()->GetPinnedItemsManager()->AddPinnedItem(pin_url.spec(), "A");
  ASSERT_FALSE(item_id.empty());

  // Laid out, not merely built: a release only clicks inside the row's bounds.
  ASSERT_TRUE(base::test::RunUntil([&]() {
    FolderTabRow* candidate = ClosedRow("a.test");
    return candidate && candidate->width() > 10 && candidate->height() > 10;
  }));
  // A row opens its pin on release; the press alone may still be a drag.
  FolderTabRow* row = ClosedRow("a.test");
  const auto mouse = [](ui::EventType type) {
    return ui::MouseEvent(type, gfx::Point(5, 5), gfx::Point(5, 5),
                          ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                          ui::EF_LEFT_MOUSE_BUTTON);
  };
  row->OnMousePressed(mouse(ui::EventType::kMousePressed));
  // Releasing materializes the tab, which rebuilds the section and deletes
  // |row|; it is not touched again.
  row->OnMouseReleased(mouse(ui::EventType::kMouseReleased));

  ASSERT_TRUE(base::test::RunUntil([&]() { return TabForItem(item_id); }));
  content::WebContents* pinned = TabForItem(item_id);
  ASSERT_TRUE(content::WaitForLoadStop(pinned));
  ASSERT_FALSE(model()->IsTabPinned(model()->GetIndexOfWebContents(pinned)));

  // Off the target: a sign-in redirect or a renamed page does the same.
  ASSERT_TRUE(content::NavigateToURL(pinned, TestURL("c.test")));
  ASSERT_EQ(item_id, GetPinnedItemIdForTab(pinned));

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return TabStateRecordsPinnedItem(item_id); }));
}

IN_PROC_BROWSER_TEST_F(AvoraPinnedRestoreBrowserTest,
                       PinnedTabOffItsTargetStaysPinned) {
  const std::string item_id = PinnedItemOnHost("a.test");
  ASSERT_FALSE(item_id.empty());

  ASSERT_TRUE(base::test::RunUntil([&]() { return TabOnHost("c.test"); }));
  content::WebContents* restored = TabOnHost("c.test");

  // Still the pin's tab, so it has the pin's row and no daily row.
  EXPECT_EQ(item_id, GetPinnedItemIdForTab(restored));
  EXPECT_TRUE(base::test::RunUntil([&]() { return LiveRow(restored); }));
  EXPECT_FALSE(ClosedRow("a.test"));
}

}  // namespace avora
