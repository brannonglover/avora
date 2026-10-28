// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_account_button.h"

#include <string>
#include <vector>

#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/avora/avora_local_user.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/sync/avora_sync_config.h"
#include "chrome/browser/avora/sync/avora_tab_sync_service.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/url_loader_interceptor.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/test/test_utils.h"
#include "ui/base/window_open_disposition.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/types/event_type.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace avora {

namespace {

// The first view under |root| that is a |T| and, when |name| is given, whose
// accessible name is |name|.
template <typename T>
T* FindView(views::View* root, const std::u16string& name = u"") {
  if (!root) {
    return nullptr;
  }
  if (auto* found = views::AsViewClass<T>(root)) {
    if (name.empty() || found->GetViewAccessibility().GetCachedName() == name) {
      return found;
    }
  }
  for (views::View* child : root->children()) {
    if (T* found = FindView<T>(child, name)) {
      return found;
    }
  }
  return nullptr;
}

constexpr char kEmail[] = "me@example.com";
constexpr char kCode[] = "123456";

// Answers the Avora account's requests the way Supabase does, enough to get
// TabSyncService signed in and through a sync of an empty account: reads of
// the shared rows find none, and writes are accepted and ignored.
class FakeSupabaseAuth {
 public:
  FakeSupabaseAuth()
      : host_(GURL(sync::kSupabaseUrl).host()),
        interceptor_(base::BindRepeating(&FakeSupabaseAuth::Intercept,
                                         base::Unretained(this))) {}

 private:
  bool Intercept(content::URLLoaderInterceptor::RequestParams* params) {
    if (params->url_request.url.host() != host_) {
      return false;
    }
    const std::string path(params->url_request.url.path());
    std::string status = "200 OK";
    std::string body = "{}";
    if (path.starts_with("/rest/v1/rpc/")) {
      status = "204 No Content";
      body = "";
    } else if (path.starts_with("/rest/v1/") &&
               params->url_request.method == "GET") {
      body = "[]";
    } else if (path == "/auth/v1/verify") {
      if (network::GetUploadData(params->url_request)
              .find(base::StrCat({"\"token\":\"", kCode, "\""})) !=
          std::string::npos) {
        body = base::StrCat(
            {R"({"access_token":"access","token_type":"bearer",)",
             R"("expires_in":3600,"refresh_token":"refresh",)",
             R"("user":{"id":"user-1","email":")", kEmail, R"("}})"});
      } else {
        status = "403 Forbidden";
        body = R"({"code":403,"error_code":"otp_expired"})";
      }
    }
    content::URLLoaderInterceptor::WriteResponse(
        base::StrCat({"HTTP/1.1 ", status,
                      "\nContent-Type: application/json\n\n"}),
        body, params->client.get());
    return true;
  }

  const std::string host_;
  content::URLLoaderInterceptor interceptor_;
};

void Click(views::Button* button) {
  views::test::ButtonTestApi(button).NotifyClick(ui::MouseEvent(
      ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
      ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON, 0));
}

}  // namespace

// The avatar in the spaces bar: one for the person, above every Space's
// identity, and the way into Settings.
class AvoraAccountButtonBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpInProcessBrowserTestFixture() override {
    InProcessBrowserTest::SetUpInProcessBrowserTestFixture();
    // A signed-in test sees its first upload in well under a second.
    sync::TabSyncService::SetUploadDelaysForTesting(base::Milliseconds(100),
                                                    base::Seconds(1));
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    // The machine's own account would make these tests depend on who runs
    // them, and has no picture on most bots anyway.
    LocalUser user;
    user.full_name = u"Brannon Glover";
    AccountButton::SetLocalUserForTesting(user);
  }

 protected:
  views::View* WindowRoot() {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->GetWidget()
        ->GetRootView();
  }

  // Looked up afresh each time: the spaces bar rebuilds its buttons whenever
  // the Spaces or this window's Space change.
  AccountButton* account_button() {
    return FindView<AccountButton>(WindowRoot());
  }

  views::Widget* OpenMenu() {
    AccountButton* button = account_button();
    if (!button) {
      return nullptr;
    }
    Click(button);
    return button->menu_widget_for_testing();
  }

  views::Button* MenuRow(views::Widget* menu, const std::u16string& name) {
    return FindView<views::Button>(menu->GetRootView(), name);
  }

  views::Label* FindLabel(views::Widget* menu, const std::u16string& text) {
    return FindView<views::Label>(menu->GetRootView(), text);
  }

  // The first label in |menu| whose text contains |text|.
  views::Label* FindLabelContaining(views::Widget* menu,
                                    const std::u16string& text) {
    std::vector<views::View*> stack = {menu->GetRootView()};
    while (!stack.empty()) {
      views::View* view = stack.back();
      stack.pop_back();
      if (auto* label = views::AsViewClass<views::Label>(view);
          label && label->GetText().find(text) != std::u16string::npos) {
        return label;
      }
      for (views::View* child : view->children()) {
        stack.push_back(child);
      }
    }
    return nullptr;
  }

  views::Textfield* FindField(views::Widget* menu, const std::u16string& name) {
    return FindView<views::Textfield>(menu->GetRootView(), name);
  }
};

// Signed out of Avora there is nobody to show: the Mac account's name and
// picture stay off the avatar, so it does not look signed in already.
IN_PROC_BROWSER_TEST_F(AvoraAccountButtonBrowserTest,
                       ShowsNoOneWhileSignedOut) {
  AccountButton* button = account_button();
  ASSERT_TRUE(button);
  EXPECT_TRUE(button->GetVisible());
  EXPECT_EQ(u"", button->InitialsForTesting());
  EXPECT_EQ(u"Not signed in to Avora", button->GetTooltipText());

  views::Widget* menu = OpenMenu();
  ASSERT_TRUE(menu);
  EXPECT_TRUE(menu->IsVisible());
  EXPECT_TRUE(FindLabel(menu, u"Not signed in"));
  EXPECT_FALSE(FindLabel(menu, u"Brannon Glover"));
  EXPECT_TRUE(MenuRow(menu, u"Sign in to Avora\u2026"));
  EXPECT_TRUE(MenuRow(menu, u"Avora Settings"));
  EXPECT_TRUE(MenuRow(menu, u"Manage Extensions"));
}

// Signed in, the avatar is the person: their initials (there is no picture
// here), with their name and Avora email in the tooltip and the menu.
IN_PROC_BROWSER_TEST_F(AvoraAccountButtonBrowserTest,
                       ShowsThePersonOnceSignedIn) {
  FakeSupabaseAuth supabase;
  sync::TabSyncService* service =
      sync::TabSyncService::GetForProfile(browser()->GetProfile());
  ASSERT_TRUE(service);
  service->RequestCode(kEmail);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return service->state() == sync::TabSyncService::State::kAwaitingCode;
  }));
  service->VerifyCode(kCode);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return service->state() == sync::TabSyncService::State::kSignedIn;
  }));

  AccountButton* button = account_button();
  ASSERT_TRUE(button);
  EXPECT_EQ(u"BG", button->InitialsForTesting());
  EXPECT_EQ(u"Brannon Glover\nme@example.com", button->GetTooltipText());

  views::Widget* menu = OpenMenu();
  ASSERT_TRUE(menu);
  EXPECT_TRUE(FindLabel(menu, u"Brannon Glover"));
  EXPECT_TRUE(FindLabel(menu, u"me@example.com"));
  EXPECT_TRUE(MenuRow(menu, u"Avora Account"));
  EXPECT_FALSE(MenuRow(menu, u"Sign in to Avora\u2026"));
}

// Every Space's identity is listed under the person, the window's own one
// checked, and picking another switches this window to it.
IN_PROC_BROWSER_TEST_F(AvoraAccountButtonBrowserTest,
                       MenuListsEveryIdentityAndSwitchesSpaces) {
  SpaceManager manager(browser()->GetProfile()->GetPrefs());
  const std::string work = manager.CreateSpace("Work");
  WindowSpaceState* state = GetWindowSpaceState(browser());
  ASSERT_TRUE(state);
  const std::string original = state->active_space_id();
  const Space* original_space = manager.GetSpaceById(original);
  ASSERT_TRUE(original_space);

  views::Widget* menu = OpenMenu();
  ASSERT_TRUE(menu);
  views::Button* original_row =
      MenuRow(menu, base::UTF8ToUTF16(original_space->name));
  views::Button* work_row = MenuRow(menu, u"Work");
  ASSERT_TRUE(original_row);
  ASSERT_TRUE(work_row);
  EXPECT_EQ(ax::mojom::CheckedState::kTrue,
            original_row->GetViewAccessibility().GetCheckedState());
  EXPECT_NE(ax::mojom::CheckedState::kTrue,
            work_row->GetViewAccessibility().GetCheckedState());

  const base::WeakPtr<views::Widget> weak_menu = menu->GetWeakPtr();
  Click(work_row);
  EXPECT_EQ(work, state->active_space_id());
  ASSERT_TRUE(base::test::RunUntil([&] { return !weak_menu; }));

  // The bar was rebuilt for the new Space; the avatar is still there.
  EXPECT_TRUE(account_button());
}

IN_PROC_BROWSER_TEST_F(AvoraAccountButtonBrowserTest,
                       SettingsRowOpensAvoraSettings) {
  views::Widget* menu = OpenMenu();
  ASSERT_TRUE(menu);
  views::Button* settings = MenuRow(menu, u"Avora Settings");
  ASSERT_TRUE(settings);
  Click(settings);

  content::WebContents* active =
      browser()->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(active);
  EXPECT_EQ(GURL("chrome://avora-settings/"), active->GetVisibleURL());
}

// Signing in happens in the menu, over the avatar: the email slides in, then
// the code, then the menu again with the person on it.  The page the user was
// on is never left.
IN_PROC_BROWSER_TEST_F(AvoraAccountButtonBrowserTest,
                       SignsInWithoutLeavingThePage) {
  FakeSupabaseAuth supabase;
  TabStripModel* tabs = browser()->GetTabStripModel();
  const int tab_count = tabs->count();
  content::WebContents* const page = tabs->GetActiveWebContents();

  views::Widget* menu = OpenMenu();
  ASSERT_TRUE(menu);
  Click(MenuRow(menu, u"Sign in to Avora\u2026"));

  views::Textfield* email = nullptr;
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return email = FindField(menu, u"Email"); }));
  email->SetText(base::UTF8ToUTF16(std::string(kEmail)));
  Click(MenuRow(menu, u"Send code"));

  views::Textfield* code = nullptr;
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return code = FindField(menu, u"Code"); }));
  EXPECT_TRUE(FindLabel(menu, u"We emailed a code to me@example.com. It can "
                              u"take a minute to arrive."));
  code->SetText(base::UTF8ToUTF16(std::string(kCode)));
  Click(MenuRow(menu, u"Sign in"));

  ASSERT_TRUE(base::test::RunUntil([&] {
    return FindLabel(menu, u"me@example.com") && !FindField(menu, u"Code");
  }));
  EXPECT_TRUE(FindLabel(menu, u"Brannon Glover"));
  EXPECT_TRUE(MenuRow(menu, u"Avora Account"));
  EXPECT_EQ(u"BG", account_button()->InitialsForTesting());

  // And says when the account's tabs last synced.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return FindLabelContaining(menu, u"tabs synced");
  }));
  EXPECT_TRUE(FindLabelContaining(menu, u"just now"));

  EXPECT_EQ(tab_count, tabs->count());
  EXPECT_EQ(page, tabs->GetActiveWebContents());
}

// A wrong code is said so under the field, which stays for another try.
IN_PROC_BROWSER_TEST_F(AvoraAccountButtonBrowserTest, WrongCodeIsExplained) {
  FakeSupabaseAuth supabase;
  views::Widget* menu = OpenMenu();
  ASSERT_TRUE(menu);
  Click(MenuRow(menu, u"Sign in to Avora\u2026"));
  views::Textfield* email = nullptr;
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return email = FindField(menu, u"Email"); }));
  email->SetText(base::UTF8ToUTF16(std::string(kEmail)));
  Click(MenuRow(menu, u"Send code"));
  views::Textfield* code = nullptr;
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return code = FindField(menu, u"Code"); }));

  code->SetText(u"999999");
  Click(MenuRow(menu, u"Sign in"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return FindLabel(menu, u"That code is wrong or has expired. Check the "
                           u"email, or send a new code.");
  }));
  EXPECT_TRUE(FindField(menu, u"Code"));
  EXPECT_TRUE(MenuRow(menu, u"Sign in")->GetEnabled());
}

// Back from the code returns to the email, and back from there to the menu.
IN_PROC_BROWSER_TEST_F(AvoraAccountButtonBrowserTest, BackStepsOutOfSignIn) {
  views::Widget* menu = OpenMenu();
  ASSERT_TRUE(menu);
  Click(MenuRow(menu, u"Sign in to Avora\u2026"));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return FindField(menu, u"Email"); }));
  Click(MenuRow(menu, u"Back"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !FindField(menu, u"Email") &&
           MenuRow(menu, u"Sign in to Avora\u2026");
  }));
}

// chrome://avora-settings/#account loads as a page of its own -- the
// signed-in "Avora Account" row opens it -- in every Space, including one
// signed in separately.  The settings page used to refuse any URL with a
// fragment.
IN_PROC_BROWSER_TEST_F(AvoraAccountButtonBrowserTest,
                       AccountSettingsLoadInEverySpace) {
  SpaceManager manager(browser()->GetProfile()->GetPrefs());
  const std::string work = manager.CreateSpace("Work");
  WindowSpaceState* state = GetWindowSpaceState(browser());
  ASSERT_TRUE(state);

  for (const std::string& space_id : {state->active_space_id(), work}) {
    state->SetActiveSpaceId(space_id);
    ui_test_utils::NavigateToURLWithDisposition(
        browser(), GURL("chrome://avora-settings/#account"),
        WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
    content::WebContents* active =
        browser()->GetTabStripModel()->GetActiveWebContents();
    ASSERT_TRUE(active);
    EXPECT_EQ(GURL("chrome://avora-settings/#account"),
              active->GetLastCommittedURL())
        << space_id;
    EXPECT_EQ(true, content::EvalJs(active,
                                    "!!document.getElementById('account')"))
        << space_id;
  }
}

// A second click on the avatar closes the menu it opened.
IN_PROC_BROWSER_TEST_F(AvoraAccountButtonBrowserTest, SecondClickClosesMenu) {
  views::Widget* menu = OpenMenu();
  ASSERT_TRUE(menu);
  const base::WeakPtr<views::Widget> weak_menu = menu->GetWeakPtr();
  Click(account_button());
  ASSERT_TRUE(base::test::RunUntil([&] { return !weak_menu; }));
}

}  // namespace avora
