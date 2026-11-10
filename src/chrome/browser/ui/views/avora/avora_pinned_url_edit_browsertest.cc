// Copyright 2026 Avora. All rights reserved.

#include <string>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/avora/avora_pinned_items.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_materializer.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_tab_marker.h"
#include "chrome/browser/ui/views/avora/avora_pinned_section_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/base/models/menu_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/window_open_disposition.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/window/dialog_delegate.h"
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

// Where a pin that has never been opened in this test points after a
// restart: the embedded test server's port changes between runs, so the
// restart test edits to an address that does not depend on it.
constexpr char kStableTarget[] = "https://edited.example/inbox";

}  // namespace

// "Edit URL…" on a pinned row changes the URL the pin points at -- its
// target -- while it stays the same pin: same id, same place, and, if open,
// the same tab.
class AvoraPinnedUrlEditBrowserTest : public InProcessBrowserTest {
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

  views::View* BrowserRoot() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

  AvoraPinnedSectionView* Section() {
    return FindView<AvoraPinnedSectionView>(
        BrowserRoot(), base::BindRepeating([](AvoraPinnedSectionView*) {
          return true;
        }));
  }

  PinnedItemsManager* Items() { return Section()->GetPinnedItemsManager(); }

  PinnedTabRow* LiveRow(content::WebContents* contents) {
    return FindView<PinnedTabRow>(
        BrowserRoot(), base::BindRepeating(
                           [](content::WebContents* wc, PinnedTabRow* row) {
                             return row->web_contents() == wc;
                           },
                           contents));
  }

  FolderTabRow* ClosedRow(const GURL& url) {
    return FindView<FolderTabRow>(
        BrowserRoot(), base::BindRepeating(
                           [](const std::string& spec, FolderTabRow* row) {
                             return row->url() == spec;
                           },
                           url.spec()));
  }

  // Opens |url| in the active tab and pins it with a kPinned record, as the
  // pin gesture does.  Returns the item id.
  std::string PinActiveTab(const GURL& url) {
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
    content::WebContents* contents =
        browser()->GetTabStripModel()->GetActiveWebContents();
    PinAndCreatePinnedItem(browser()->GetTabStripModel(), Items(), contents);
    return GetPinnedItemIdForTab(contents);
  }

  // Leaves the pin with no live tab: a daily tab to land on, then the pin's
  // tab closed.
  void ClosePinnedTab(content::WebContents* pinned) {
    ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), TestURL("daily.test"),
        WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    TabStripModel* model = browser()->GetTabStripModel();
    model->CloseWebContentsAt(model->GetIndexOfWebContents(pinned),
                              TabCloseTypes::CLOSE_NONE);
  }

  // Picks "Edit URL…" off |menu|, as a click would, and returns the dialog
  // it opens.
  views::Widget* PickEditUrl(ui::MenuModel* menu) {
    for (size_t i = 0; i < menu->GetItemCount(); ++i) {
      if (menu->GetLabelAt(i) == u"Edit URL…") {
        menu->ActivatedAt(i);
        return Section()->pinned_url_dialog_for_testing();
      }
    }
    ADD_FAILURE() << "No \"Edit URL…\" on the menu";
    return nullptr;
  }

  static views::Textfield* FieldOf(views::Widget* dialog) {
    return FindView<views::Textfield>(
        dialog->GetRootView(),
        base::BindRepeating([](views::Textfield*) { return true; }));
  }

  // Replaces the field's text the way typing over its selection would, so
  // the dialog hears about it as it does a keystroke.
  static void Type(views::Textfield* field, const std::u16string& text) {
    field->SelectAll(/*reversed=*/false);
    field->InsertText(
        text,
        ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  }

  static views::DialogDelegate* DelegateOf(views::Widget* dialog) {
    return dialog->widget_delegate()->AsDialogDelegate();
  }

  static bool SaveEnabled(views::Widget* dialog) {
    return DelegateOf(dialog)->IsDialogButtonEnabled(
        ui::mojom::DialogButton::kOk);
  }

  // Types |url| into the dialog and presses Save.
  void SaveUrl(views::Widget* dialog, const GURL& url) {
    Type(FieldOf(dialog), base::UTF8ToUTF16(url.spec()));
    ASSERT_TRUE(SaveEnabled(dialog));
    DelegateOf(dialog)->AcceptDialog();
    ASSERT_TRUE(base::test::RunUntil(
        [&]() { return !Section()->pinned_url_dialog_for_testing(); }));
  }
};

// A pin with no tab: the row and the persisted item move to the new target,
// and nothing is opened.
IN_PROC_BROWSER_TEST_F(AvoraPinnedUrlEditBrowserTest, EditsClosedPin) {
  TabStripModel* model = browser()->GetTabStripModel();
  const GURL old_url = TestURL("a.test");
  const GURL new_url = TestURL("c.test");
  const std::string item_id = PinActiveTab(old_url);
  ASSERT_FALSE(item_id.empty());
  ClosePinnedTab(model->GetActiveWebContents());
  ASSERT_TRUE(base::test::RunUntil([&]() { return ClosedRow(old_url); }));
  const int tab_count = model->count();

  views::Widget* dialog = PickEditUrl(
      Section()->BuildPersistentPinnedItemContextMenuForTesting(item_id));
  ASSERT_TRUE(dialog);
  views::Textfield* field = FieldOf(dialog);
  ASSERT_TRUE(field);
  EXPECT_EQ(base::UTF8ToUTF16(old_url.spec()), field->GetText());

  // Something that is not a page to pin keeps Save off.
  Type(field, u"javascript:alert(1)");
  EXPECT_FALSE(SaveEnabled(dialog));

  SaveUrl(dialog, new_url);

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return ClosedRow(new_url) && !ClosedRow(old_url); }));
  const std::optional<PinnedItemEntry> item = Items()->GetPinnedItemById(item_id);
  ASSERT_TRUE(item);
  EXPECT_EQ(new_url.spec(), item->url);
  EXPECT_EQ(1u, Items()->GetPinnedItems().size());
  EXPECT_EQ(tab_count, model->count());
}

// A pin whose tab is showing the old target: the tab is showing "the pin",
// so it follows the pin to its new target -- as a navigation, so Back still
// reaches the old page -- and stays the pin's tab.
IN_PROC_BROWSER_TEST_F(AvoraPinnedUrlEditBrowserTest,
                       OpenPinOnOldTargetFollowsTheEdit) {
  const GURL old_url = TestURL("a.test");
  const GURL new_url = TestURL("c.test");
  const std::string item_id = PinActiveTab(old_url);
  ASSERT_FALSE(item_id.empty());
  content::WebContents* pinned =
      browser()->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(base::test::RunUntil([&]() { return LiveRow(pinned); }));

  views::Widget* dialog =
      PickEditUrl(Section()->BuildPinnedTabContextMenuForTesting(pinned));
  ASSERT_TRUE(dialog);
  SaveUrl(dialog, new_url);

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return pinned->GetLastCommittedURL() == new_url; }));
  EXPECT_TRUE(content::WaitForLoadStop(pinned));
  EXPECT_EQ(item_id, GetPinnedItemIdForTab(pinned));
  EXPECT_TRUE(pinned->GetController().CanGoBack());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    PinnedTabRow* row = LiveRow(pinned);
    return row && row->target_url() == new_url;
  }));
  EXPECT_FALSE(HasLeftPinnedTarget(Items(), pinned));
}

// A pin whose tab has browsed away is left where the user took it.  Only its
// way home changes: returning to the pin now leads to the new target.
IN_PROC_BROWSER_TEST_F(AvoraPinnedUrlEditBrowserTest,
                       OpenPinThatWanderedOffStaysPut) {
  const GURL old_url = TestURL("a.test");
  const GURL elsewhere = TestURL("b.test");
  const GURL new_url = TestURL("c.test");
  const std::string item_id = PinActiveTab(old_url);
  ASSERT_FALSE(item_id.empty());
  content::WebContents* pinned =
      browser()->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), elsewhere));
  ASSERT_TRUE(HasLeftPinnedTarget(Items(), pinned));
  ASSERT_TRUE(base::test::RunUntil([&]() { return LiveRow(pinned); }));

  views::Widget* dialog =
      PickEditUrl(Section()->BuildPinnedTabContextMenuForTesting(pinned));
  ASSERT_TRUE(dialog);
  SaveUrl(dialog, new_url);

  ASSERT_TRUE(base::test::RunUntil([&]() {
    PinnedTabRow* row = LiveRow(pinned);
    return row && row->target_url() == new_url;
  }));
  EXPECT_EQ(elsewhere, pinned->GetLastCommittedURL());
  EXPECT_FALSE(pinned->IsLoading());
  EXPECT_EQ(item_id, GetPinnedItemIdForTab(pinned));
  EXPECT_TRUE(HasLeftPinnedTarget(Items(), pinned));

  ReturnTabToPinnedTarget(Items(), pinned);
  EXPECT_TRUE(content::WaitForLoadStop(pinned));
  EXPECT_EQ(new_url, pinned->GetLastCommittedURL());
}

// Another pin in the Space already opens that page: Save stays off, as
// pinning the same page twice is refused.
IN_PROC_BROWSER_TEST_F(AvoraPinnedUrlEditBrowserTest, RefusesADuplicate) {
  const GURL first = TestURL("a.test");
  const GURL second = TestURL("b.test");
  const std::string item_id = PinActiveTab(first);
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), second, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  PinAndCreatePinnedItem(browser()->GetTabStripModel(), Items(),
                         browser()->GetTabStripModel()->GetActiveWebContents());
  ASSERT_EQ(2u, Items()->GetPinnedItems().size());
  content::WebContents* first_tab =
      browser()->GetTabStripModel()->GetWebContentsAt(0);
  ASSERT_EQ(item_id, GetPinnedItemIdForTab(first_tab));

  views::Widget* dialog =
      PickEditUrl(Section()->BuildPinnedTabContextMenuForTesting(first_tab));
  ASSERT_TRUE(dialog);
  Type(FieldOf(dialog), base::UTF8ToUTF16(second.spec()));
  EXPECT_FALSE(SaveEnabled(dialog));
  EXPECT_FALSE(Section()->EditPinnedItemUrl(item_id, second.spec()));
  EXPECT_EQ(first.spec(), Items()->GetPinnedItemUrlById(item_id));
  dialog->CloseNow();
}

// The edit is to the persisted pin, so it is still there after a restart.
IN_PROC_BROWSER_TEST_F(AvoraPinnedUrlEditBrowserTest,
                       PRE_EditSurvivesRestart) {
  const std::string item_id = PinActiveTab(TestURL("a.test"));
  ASSERT_FALSE(item_id.empty());
  ClosePinnedTab(browser()->GetTabStripModel()->GetActiveWebContents());
  ASSERT_TRUE(Section()->EditPinnedItemUrl(item_id, kStableTarget));
  ASSERT_EQ(kStableTarget, Items()->GetPinnedItemUrlById(item_id));
}

IN_PROC_BROWSER_TEST_F(AvoraPinnedUrlEditBrowserTest, EditSurvivesRestart) {
  const std::vector<PinnedItemEntry> items = Items()->GetPinnedItems();
  ASSERT_EQ(1u, items.size());
  EXPECT_EQ(kStableTarget, items[0].url);
  EXPECT_TRUE(
      base::test::RunUntil([&]() { return ClosedRow(GURL(kStableTarget)); }));
}

}  // namespace avora
