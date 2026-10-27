// Copyright 2026 Avora. All rights reserved.
//
// Tab sync end to end inside the browser, against a fake Supabase: the tab
// list the watcher collects from real windows, signing in through
// chrome://avora-settings, the debounced upsert of this device's row, and the
// delete on sign-out.

#include <optional>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_tab_space.h"
#include "chrome/browser/avora/sync/avora_sync_config.h"
#include "chrome/browser/avora/sync/avora_sync_session_store.h"
#include "chrome/browser/avora/sync/avora_tab_snapshot.h"
#include "chrome/browser/avora/sync/avora_tab_sync_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_tab_sync_watcher.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/url_loader_interceptor.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "services/network/test/test_utils.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace avora {
namespace {

using sync::TabSyncService;

constexpr char kEmail[] = "me@example.com";
constexpr char kCode[] = "123456";

// What Supabase would see.
struct Request {
  std::string method;
  std::string path;  // Path and query.
  std::string body;
  std::string apikey;
  std::string authorization;
  std::string prefer;
};

// Answers the Supabase project's URLs the way Supabase does and records every
// request. Everything else goes to the network (the embedded test server).
// The interceptor may call in on the IO thread, hence the lock.
class FakeSupabase {
 public:
  FakeSupabase()
      : host_(GURL(sync::kSupabaseUrl).host()),
        interceptor_(base::BindRepeating(&FakeSupabase::Intercept,
                                         base::Unretained(this))) {}

  std::vector<Request> Requests() {
    base::AutoLock lock(lock_);
    return requests_;
  }

  std::vector<Request> RequestsTo(std::string_view method,
                                  std::string_view path_prefix) {
    std::vector<Request> matching;
    for (Request& request : Requests()) {
      if (request.method == method && request.path.starts_with(path_prefix)) {
        matching.push_back(std::move(request));
      }
    }
    return matching;
  }

  std::vector<Request> Upserts() {
    return RequestsTo("POST", "/rest/v1/devices");
  }

 private:
  bool Intercept(content::URLLoaderInterceptor::RequestParams* params) {
    const network::ResourceRequest& resource = params->url_request;
    if (resource.url.host() != host_) {
      return false;
    }
    Request request;
    request.method = resource.method;
    request.path = resource.url.PathForRequest();
    request.body = network::GetUploadData(resource);
    request.apikey = resource.headers.GetHeader("apikey").value_or("");
    request.authorization =
        resource.headers.GetHeader("Authorization").value_or("");
    request.prefer = resource.headers.GetHeader("Prefer").value_or("");

    std::string status = "200 OK";
    std::string body = "{}";
    if (request.path == "/auth/v1/verify") {
      if (request.body.find(base::StrCat({"\"token\":\"", kCode, "\""})) !=
          std::string::npos) {
        body = Session("access-1", "refresh-1");
      } else {
        status = "403 Forbidden";
        body = R"({"code":403,"error_code":"otp_expired"})";
      }
    } else if (request.path.starts_with("/auth/v1/token")) {
      body = Session("access-2", "refresh-2");
    } else if (request.path.starts_with("/auth/v1/logout")) {
      status = "204 No Content";
      body = "";
    } else if (request.path.starts_with("/rest/v1/devices")) {
      status = request.method == "DELETE" ? "204 No Content" : "201 Created";
      body = "";
    }
    {
      base::AutoLock lock(lock_);
      requests_.push_back(std::move(request));
    }
    content::URLLoaderInterceptor::WriteResponse(
        base::StrCat({"HTTP/1.1 ", status,
                      "\nContent-Type: application/json\n\n"}),
        body, params->client.get());
    return true;
  }

  static std::string Session(std::string_view access, std::string_view refresh) {
    return base::StrCat(
        {R"({"access_token":")", access,
         R"(","token_type":"bearer","expires_in":3600,"refresh_token":")",
         refresh, R"(","user":{"id":"user-1","email":")", kEmail, R"("}})"});
  }

  const std::string host_;
  base::Lock lock_;
  std::vector<Request> requests_;
  content::URLLoaderInterceptor interceptor_;
};

std::vector<std::string> UrlsIn(const base::ListValue& tabs) {
  std::vector<std::string> urls;
  for (const base::Value& tab : tabs) {
    urls.push_back(*tab.GetDict().FindString("url"));
  }
  return urls;
}

}  // namespace

class AvoraTabSyncBrowserTest : public InProcessBrowserTest {
 protected:
  // Long enough that a page load's URL and title changes all land in one
  // upload, short enough to keep the test quick.
  static constexpr base::TimeDelta kQuiet = base::Seconds(1);
  static constexpr base::TimeDelta kMax = base::Seconds(5);

  void SetUpInProcessBrowserTestFixture() override {
    InProcessBrowserTest::SetUpInProcessBrowserTestFixture();
    TabSyncService::SetUploadDelaysForTesting(kQuiet, kMax);
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    supabase_.emplace();
  }

  void TearDownOnMainThread() override {
    supabase_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  GURL TestURL(const std::string& host) {
    return embedded_test_server()->GetURL(host, "/title2.html");
  }

  TabSyncService* service() {
    return TabSyncService::GetForProfile(browser()->GetProfile());
  }

  // The tabs array the service would upload right now.
  base::ListValue CurrentTabs() {
    std::optional<std::vector<sync::OpenTab>> tabs =
        CollectOpenTabs(browser()->GetProfile());
    return tabs ? sync::BuildTabsList(*tabs) : base::ListValue();
  }

  static base::DictValue ParseBody(const Request& request) {
    std::optional<base::DictValue> dict =
        base::JSONReader::ReadDict(request.body, base::JSON_PARSE_RFC);
    EXPECT_TRUE(dict) << request.body;
    return dict ? std::move(*dict) : base::DictValue();
  }

  content::WebContents* active_contents() {
    return browser()->GetTabStripModel()->GetActiveWebContents();
  }

  void OpenSettings() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                             GURL("chrome://avora-settings")));
  }

  std::string PageText(std::string_view id) {
    return content::EvalJs(active_contents(),
                           base::StrCat({"document.getElementById('", id,
                                         "').textContent"}))
        .ExtractString();
  }

  bool WaitForPageText(std::string_view id, std::string_view text) {
    return base::test::RunUntil(
        [&] { return PageText(id).find(text) != std::string::npos; });
  }

  // Signs in through the Account section of chrome://avora-settings, typing
  // into its real email and code fields.
  void SignInThroughSettings() {
    OpenSettings();
    ASSERT_TRUE(WaitForPageText("account-state", "Not signed in."));
    ASSERT_TRUE(content::ExecJs(
        active_contents(),
        base::StrCat({"document.getElementById('account-email').value = '",
                      kEmail,
                      "';"
                      "document.getElementById('account-send').click();"})));
    ASSERT_TRUE(WaitForPageText("account-state", "Enter the code sent to"));
    ASSERT_TRUE(content::ExecJs(
        active_contents(),
        base::StrCat({"document.getElementById('account-code').value = '",
                      kCode,
                      "';"
                      "document.getElementById('account-verify').click();"})));
    ASSERT_TRUE(WaitForPageText("account-state", "Signed in as"));
  }

  bool WaitForUpserts(size_t count) {
    return base::test::RunUntil(
        [&] { return supabase_->Upserts().size() >= count; });
  }

  void Wait(base::TimeDelta delay) {
    base::RunLoop run_loop;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), delay);
    run_loop.Run();
  }

  std::optional<FakeSupabase> supabase_;
};

// The collected tabs cover every window and Space, leave out local pages, fold
// the per-window copies of a daily tab, and carry the contract's fields.
IN_PROC_BROWSER_TEST_F(AvoraTabSyncBrowserTest, CollectsTabsAcrossWindows) {
  const GURL a = TestURL("a.example");
  const GURL b = TestURL("b.example");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), a));
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), b, WindowOpenDisposition::NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("chrome://version"),
      WindowOpenDisposition::NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  browser()->GetTabStripModel()->SetTabPinned(0, true);

  // A second window of the same Space holds its own copy of each daily tab.
  chrome::ExecuteCommand(browser(), IDC_NEW_WINDOW);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return GetAllBrowserWindowInterfaces().size() >= 2;
  }));

  base::ListValue tabs = CurrentTabs();
  const std::vector<std::string> urls = UrlsIn(tabs);
  // b.example once, however many windows show it; no chrome:// page.
  EXPECT_EQ(std::ranges::count(urls, b.spec()), 1) << tabs.DebugString();
  EXPECT_EQ(std::ranges::count(urls, a.spec()), 1) << tabs.DebugString();
  for (const std::string& url : urls) {
    EXPECT_TRUE(GURL(url).SchemeIsHTTPOrHTTPS()) << url;
  }

  const SpaceManager spaces(browser()->GetProfile()->GetPrefs());
  for (const base::Value& value : tabs) {
    const base::DictValue& tab = value.GetDict();
    EXPECT_TRUE(tab.FindString("title"));
    EXPECT_TRUE(tab.FindBool("pinned"));
    EXPECT_TRUE(tab.FindDouble("lastActiveAt"));
    if (const std::string* space = tab.FindString("space")) {
      EXPECT_FALSE(space->empty());
    }
    if (*tab.FindString("url") == a.spec()) {
      EXPECT_EQ(tab.FindBool("pinned"), true);
      EXPECT_EQ(*tab.FindString("title"), "Title Of Awesomeness");
      // a.example was tagged with the window's Space; its name is what is sent.
      const std::string space_id = GetTabSpaceId(
          browser()->GetTabStripModel()->GetWebContentsAt(0));
      if (const Space* space = spaces.GetSpaceById(space_id)) {
        EXPECT_EQ(*tab.FindString("space"), space->name);
      }
    }
    if (*tab.FindString("url") == b.spec()) {
      EXPECT_EQ(tab.FindBool("pinned"), false);
    }
  }
}

// Signing in through the settings page uploads this device's row at once, a
// burst of tab changes becomes one more upload, and signing out deletes the
// row before forgetting the session.
IN_PROC_BROWSER_TEST_F(AvoraTabSyncBrowserTest, SignInUploadsAndSignOutDeletes) {
  const GURL first = TestURL("first.example");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), first, WindowOpenDisposition::NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);

  SignInThroughSettings();

  std::vector<Request> otp = supabase_->RequestsTo("POST", "/auth/v1/otp");
  ASSERT_EQ(otp.size(), 1u);
  EXPECT_EQ(otp[0].apikey, sync::kSupabasePublishableKey);
  EXPECT_EQ(*ParseBody(otp[0]).FindString("email"), kEmail);
  std::vector<Request> verify =
      supabase_->RequestsTo("POST", "/auth/v1/verify");
  ASSERT_EQ(verify.size(), 1u);
  EXPECT_EQ(*ParseBody(verify[0]).FindString("type"), "email");
  EXPECT_EQ(*ParseBody(verify[0]).FindString("token"), kCode);

  // The sign-in upload, sent without waiting for quiet.
  ASSERT_TRUE(WaitForUpserts(1));
  Request upsert = supabase_->Upserts()[0];
  EXPECT_EQ(upsert.path, "/rest/v1/devices?on_conflict=id");
  EXPECT_EQ(upsert.apikey, sync::kSupabasePublishableKey);
  EXPECT_EQ(upsert.authorization, "Bearer access-1");
  EXPECT_EQ(upsert.prefer, "resolution=merge-duplicates,return=minimal");
  base::DictValue row = ParseBody(upsert);
  const std::string device_id = *row.FindString("id");
  EXPECT_EQ(device_id, browser()->GetProfile()->GetPrefs()->GetString(
                           sync::SessionStore::kDeviceIdPref));
  EXPECT_FALSE(row.FindString("name")->empty());
  EXPECT_EQ(*row.FindString("platform"), sync::CurrentPlatform());
  ASSERT_TRUE(row.FindList("tabs"));
  std::vector<std::string> urls = UrlsIn(*row.FindList("tabs"));
  EXPECT_TRUE(std::ranges::contains(urls, first.spec()));
  // chrome://avora-settings itself is open, and is not sent.
  for (const std::string& url : urls) {
    EXPECT_TRUE(GURL(url).SchemeIsHTTPOrHTTPS()) << url;
  }

  // The page shows who is signed in and that the upload landed; it never
  // learns a token.
  EXPECT_TRUE(WaitForPageText("account-state", "synced at"));
  EXPECT_NE(PageText("account-state").find(kEmail), std::string::npos);
  const std::string page =
      content::EvalJs(active_contents(), "document.documentElement.outerHTML")
          .ExtractString();
  EXPECT_EQ(page.find("access-1"), std::string::npos);
  EXPECT_EQ(page.find("refresh-1"), std::string::npos);

  // Three tabs opened back to back, each loading, retitling and becoming
  // active in turn: one upload.
  const GURL x = TestURL("x.example");
  const GURL y = TestURL("y.example");
  const GURL z = TestURL("z.example");
  for (const GURL& url : {x, y, z}) {
    ui_test_utils::NavigateToURLWithDisposition(
        browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_NO_WAIT);
  }
  ASSERT_TRUE(WaitForUpserts(2));
  Wait(kQuiet * 2);
  std::vector<Request> upserts = supabase_->Upserts();
  ASSERT_EQ(upserts.size(), 2u);
  urls = UrlsIn(*ParseBody(upserts[1]).FindList("tabs"));
  EXPECT_TRUE(std::ranges::contains(urls, x.spec()));
  EXPECT_TRUE(std::ranges::contains(urls, y.spec()));
  EXPECT_TRUE(std::ranges::contains(urls, z.spec()));
  EXPECT_TRUE(std::ranges::contains(urls, first.spec()));

  // Closing a tab is uploaded too.
  TabStripModel* model = browser()->GetTabStripModel();
  for (int i = 0; i < model->count(); ++i) {
    if (model->GetWebContentsAt(i)->GetVisibleURL() == y) {
      model->CloseWebContentsAt(i, TabCloseTypes::CLOSE_NONE);
      break;
    }
  }
  ASSERT_TRUE(WaitForUpserts(3));
  urls = UrlsIn(*ParseBody(supabase_->Upserts()[2]).FindList("tabs"));
  EXPECT_FALSE(std::ranges::contains(urls, y.spec()));
  EXPECT_TRUE(std::ranges::contains(urls, x.spec()));

  // Sign out from the page: the row is deleted with the session's token, then
  // the session is revoked and forgotten.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           GURL("chrome://avora-settings")));
  ASSERT_TRUE(WaitForPageText("account-state", "Signed in as"));
  ASSERT_TRUE(content::ExecJs(
      active_contents(), "document.getElementById('account-sign-out').click()"));
  ASSERT_TRUE(WaitForPageText("account-state", "Not signed in."));
  EXPECT_NE(PageText("account-message").find("removed from your account"),
            std::string::npos);

  std::vector<Request> deletes = supabase_->RequestsTo("DELETE", "/rest/v1/");
  ASSERT_EQ(deletes.size(), 1u);
  EXPECT_EQ(deletes[0].path,
            base::StrCat({"/rest/v1/devices?id=eq.", device_id}));
  EXPECT_EQ(deletes[0].authorization, "Bearer access-1");
  EXPECT_TRUE(base::test::RunUntil([&] {
    return supabase_->RequestsTo("POST", "/auth/v1/logout").size() == 1;
  }));
  EXPECT_EQ(service()->state(), TabSyncService::State::kSignedOut);
  EXPECT_TRUE(browser()
                  ->GetProfile()
                  ->GetPrefs()
                  ->GetDict(sync::SessionStore::kSessionPref)
                  .empty());

  // Signed out, tab changes upload nothing; the device keeps its id.
  const size_t upserts_before = supabase_->Upserts().size();
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), TestURL("after.example"),
      WindowOpenDisposition::NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  Wait(kQuiet * 2);
  EXPECT_EQ(supabase_->Upserts().size(), upserts_before);
  EXPECT_EQ(browser()->GetProfile()->GetPrefs()->GetString(
                sync::SessionStore::kDeviceIdPref),
            device_id);
}

IN_PROC_BROWSER_TEST_F(AvoraTabSyncBrowserTest, WrongCodeKeepsAskingForIt) {
  OpenSettings();
  ASSERT_TRUE(WaitForPageText("account-state", "Not signed in."));
  ASSERT_TRUE(content::ExecJs(
      active_contents(),
      base::StrCat({"document.getElementById('account-email').value = '",
                    kEmail,
                    "';"
                    "document.getElementById('account-send').click();"})));
  ASSERT_TRUE(WaitForPageText("account-state", "Enter the code sent to"));
  ASSERT_TRUE(content::ExecJs(
      active_contents(),
      "document.getElementById('account-code').value = '654321';"
      "document.getElementById('account-verify').click();"));
  ASSERT_TRUE(WaitForPageText("account-message", "wrong or has expired"));
  EXPECT_EQ(service()->state(), TabSyncService::State::kAwaitingCode);
  EXPECT_TRUE(supabase_->Upserts().empty());
}

}  // namespace avora
