// Copyright 2026 Avora. All rights reserved.
//
// Shared tabs end to end inside the browser, against a fake Supabase that
// keeps the shared rows the way the real one does: signing in through
// chrome://avora-settings, this Mac's tabs, Spaces and pinned items going up
// as rows, another device's rows coming down into the tab strip and sidebar,
// and signing out.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/run_loop.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/avora/avora_sidebar_item.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_tab_guid.h"
#include "chrome/browser/avora/sync/avora_shared_rows.h"
#include "chrome/browser/avora/sync/avora_sync_config.h"
#include "chrome/browser/avora/sync/avora_sync_session_store.h"
#include "chrome/browser/avora/sync/avora_tab_sync_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_controller.h"
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
constexpr char kPhone[] = "00000000-0000-4000-8000-0000000000ff";

// What Supabase would see.
struct Request {
  std::string method;
  std::string path;  // Path and query.
  std::string body;
  std::string apikey;
  std::string authorization;
};

// Answers the Supabase project's URLs the way Supabase does, keeping the
// shared spaces and items rows (docs/SYNC.md) and recording every request.
// Everything else goes to the network (the embedded test server). The
// interceptor may call in on the IO thread, hence the lock.
class FakeSupabase {
 public:
  FakeSupabase()
      : host_(GURL(sync::kSupabaseUrl).host()),
        interceptor_(base::BindRepeating(&FakeSupabase::Intercept,
                                         base::Unretained(this))) {}

  std::vector<Request> RequestsTo(std::string_view method,
                                  std::string_view path_prefix) {
    base::AutoLock lock(lock_);
    std::vector<Request> matching;
    for (const Request& request : requests_) {
      if (request.method == method && request.path.starts_with(path_prefix)) {
        matching.push_back(request);
      }
    }
    return matching;
  }

  // A copy of every row of `table` ("spaces" or "items").
  std::vector<base::DictValue> Rows(const std::string& table) {
    base::AutoLock lock(lock_);
    std::vector<base::DictValue> rows;
    for (const auto& [id, row] : tables_[table]) {
      rows.push_back(row.Clone());
    }
    return rows;
  }

  std::optional<base::DictValue> Row(const std::string& table,
                                     const std::string& id) {
    base::AutoLock lock(lock_);
    const auto it = tables_[table].find(id);
    return it == tables_[table].end() ? std::nullopt
                                      : std::optional(it->second.Clone());
  }

  // Live items whose URL is `url`.
  std::vector<base::DictValue> LiveItemsWithUrl(const GURL& url) {
    std::vector<base::DictValue> found;
    for (base::DictValue& row : Rows("items")) {
      const std::string* row_url = row.FindString("url");
      if (row_url && *row_url == url.spec() && IsLive(row)) {
        found.push_back(std::move(row));
      }
    }
    return found;
  }

  static bool IsLive(const base::DictValue& row) {
    const base::Value* deleted = row.Find("deleted_at");
    return !deleted || deleted->is_none();
  }

  // Another device (the phone) writing, through the same path as Avora.
  void PhoneWrites(const std::string& table, base::DictValue change) {
    base::AutoLock lock(lock_);
    change.Set("updated_by", kPhone);
    ApplyChange(table, std::move(change));
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

    std::string status = "200 OK";
    std::string body = "{}";
    const std::string path(resource.url.path());
    base::AutoLock lock(lock_);
    if (path == "/auth/v1/verify") {
      if (request.body.find(base::StrCat({"\"token\":\"", kCode, "\""})) !=
          std::string::npos) {
        body = Session("access-1", "refresh-1");
      } else {
        status = "403 Forbidden";
        body = R"({"code":403,"error_code":"otp_expired"})";
      }
    } else if (path == "/auth/v1/token") {
      body = Session("access-2", "refresh-2");
    } else if (path == "/auth/v1/logout") {
      status = "204 No Content";
      body = "";
    } else if (path == "/rest/v1/devices") {
      status = request.method == "DELETE" ? "204 No Content" : "201 Created";
      body = "";
    } else if (path == "/rest/v1/spaces" || path == "/rest/v1/items") {
      body = Select(path.substr(std::string("/rest/v1/").size()),
                    std::string(resource.url.query()));
    } else if (path == "/rest/v1/rpc/sync_spaces" ||
               path == "/rest/v1/rpc/sync_items") {
      const std::string table =
          path.ends_with("sync_spaces") ? "spaces" : "items";
      std::optional<base::DictValue> call =
          base::JSONReader::ReadDict(request.body, base::JSON_PARSE_RFC);
      if (call && call->FindList("changes")) {
        for (const base::Value& change : *call->FindList("changes")) {
          ApplyChange(table, change.GetDict().Clone());
        }
      }
      status = "204 No Content";
      body = "";
    }
    requests_.push_back(std::move(request));
    content::URLLoaderInterceptor::WriteResponse(
        base::StrCat({"HTTP/1.1 ", status,
                      "\nContent-Type: application/json\n\n"}),
        body, params->client.get());
    return true;
  }

  // sync_items/sync_spaces: keys present are written, deleted_at is stamped
  // by the server, and every write gets a new updated_at.
  void ApplyChange(const std::string& table, base::DictValue change) {
    const std::string id = *change.FindString("id");
    const std::string now = Now();
    auto [it, created] = tables_[table].try_emplace(id);
    base::DictValue& row = it->second;
    const base::Value* old_deleted = row.Find("deleted_at");
    const bool was_deleted = old_deleted && !old_deleted->is_none();
    for (auto [key, value] : change) {
      if (key == "deleted_at") {
        if (value.is_none()) {
          row.Set(key, base::Value());
        } else if (!was_deleted) {
          row.Set(key, now);
        }
      } else {
        row.Set(key, value.Clone());
      }
    }
    if (created && !row.Find("deleted_at")) {
      row.Set("deleted_at", base::Value());
    }
    row.Set("updated_at", now);
  }

  std::string Select(const std::string& table, std::string_view query) {
    std::optional<base::Time> since;
    size_t offset = 0;
    size_t limit = 1000;
    for (std::string_view param : base::SplitStringPiece(
             query, "&", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
      const std::string value = base::UnescapeURLComponent(
          param.substr(param.find('=') + 1),
          base::UnescapeRule::URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS);
      if (param.starts_with("updated_at=gte.")) {
        since = sync::ParseServerTime(value.substr(4));
      } else if (param.starts_with("offset=")) {
        base::StringToSizeT(value, &offset);
      } else if (param.starts_with("limit=")) {
        base::StringToSizeT(value, &limit);
      }
    }
    std::vector<const base::DictValue*> rows;
    for (const auto& [id, row] : tables_[table]) {
      const std::optional<base::Time> updated =
          sync::ParseServerTime(*row.FindString("updated_at"));
      if (!since || (updated && *updated >= *since)) {
        rows.push_back(&row);
      }
    }
    std::sort(rows.begin(), rows.end(),
              [](const base::DictValue* a, const base::DictValue* b) {
                return *a->FindString("updated_at") <
                       *b->FindString("updated_at");
              });
    base::ListValue page;
    for (size_t i = offset; i < rows.size() && i < offset + limit; ++i) {
      page.Append(rows[i]->Clone());
    }
    return base::WriteJson(page).value_or("[]");
  }

  // Strictly increasing, like a server clock seen through one connection.
  std::string Now() {
    base::Time now = base::Time::Now();
    if (now <= last_now_) {
      now = last_now_ + base::Microseconds(1);
    }
    last_now_ = now;
    return sync::FormatServerTime(now);
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
  std::map<std::string, std::map<std::string, base::DictValue>> tables_;
  base::Time last_now_;
  content::URLLoaderInterceptor interceptor_;
};

}  // namespace

class AvoraTabSyncBrowserTest : public InProcessBrowserTest {
 protected:
  // Local changes go up after kPush; other devices' changes are fetched
  // every kPull.
  static constexpr base::TimeDelta kPush = base::Milliseconds(200);
  static constexpr base::TimeDelta kPull = base::Milliseconds(500);

  void SetUpInProcessBrowserTestFixture() override {
    InProcessBrowserTest::SetUpInProcessBrowserTestFixture();
    TabSyncService::SetUploadDelaysForTesting(kPush, kPull);
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

  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }

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

  // The tab showing `url` in this window, loaded or not.
  tabs::TabInterface* FindTab(const GURL& url) {
    TabStripModel* model = browser()->GetTabStripModel();
    for (int i = 0; i < model->count(); ++i) {
      content::WebContents* contents = model->GetWebContentsAt(i);
      if (contents->GetVisibleURL() == url ||
          contents->GetLastCommittedURL() == url) {
        return model->GetTabAtIndex(i);
      }
    }
    return nullptr;
  }

  // The one live shared row for `url`, once there is exactly one.
  std::optional<base::DictValue> WaitForOneRow(const GURL& url) {
    std::optional<base::DictValue> row;
    const bool found = base::test::RunUntil([&] {
      std::vector<base::DictValue> rows = supabase_->LiveItemsWithUrl(url);
      if (rows.size() == 1) {
        row = std::move(rows[0]);
        return true;
      }
      return false;
    });
    EXPECT_TRUE(found) << url;
    return row;
  }

  std::string LocalSpaceId() {
    const SpaceManager spaces(prefs());
    return spaces.GetSpaces().front().id;
  }

  void Wait(base::TimeDelta delay) {
    base::RunLoop run_loop;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), delay);
    run_loop.Run();
  }

  std::optional<FakeSupabase> supabase_;
};

// Signing in shares what this Mac already has: its Space, and its open tabs as
// today rows. The device row carries no tabs any more.
IN_PROC_BROWSER_TEST_F(AvoraTabSyncBrowserTest, SignInSharesThisMacsTabs) {
  const GURL first = TestURL("first.example");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), first, WindowOpenDisposition::NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);

  SignInThroughSettings();

  std::optional<base::DictValue> row = WaitForOneRow(first);
  ASSERT_TRUE(row);
  EXPECT_EQ(*row->FindString("kind"), "today");
  EXPECT_EQ(*row->FindString("title"), "Title Of Awesomeness");
  EXPECT_TRUE(row->FindString("position"));
  // The row's id is the tab's Avora GUID; its Space is this Mac's Space.
  tabs::TabInterface* tab = FindTab(first);
  ASSERT_TRUE(tab);
  EXPECT_EQ(*row->FindString("id"), GetTabGuid(tab->GetContents()));
  EXPECT_EQ(*row->FindString("space_id"), LocalSpaceId());
  ASSERT_TRUE(supabase_->Row("spaces", LocalSpaceId()));

  // chrome:// pages, like the settings page itself, are never shared.
  for (const base::DictValue& item : supabase_->Rows("items")) {
    EXPECT_TRUE(GURL(*item.FindString("url")).SchemeIsHTTPOrHTTPS());
  }

  // Requests carry the publishable key and the session's token.
  std::vector<Request> writes =
      supabase_->RequestsTo("POST", "/rest/v1/rpc/sync_items");
  ASSERT_FALSE(writes.empty());
  EXPECT_EQ(writes[0].apikey, sync::kSupabasePublishableKey);
  EXPECT_EQ(writes[0].authorization, "Bearer access-1");

  ASSERT_TRUE(base::test::RunUntil([&] {
    return !supabase_->RequestsTo("POST", "/rest/v1/devices").empty();
  }));
  base::DictValue device =
      ParseBody(supabase_->RequestsTo("POST", "/rest/v1/devices")[0]);
  EXPECT_EQ(*device.FindString("id"),
            prefs()->GetString(sync::SessionStore::kDeviceIdPref));
  EXPECT_FALSE(device.FindString("name")->empty());
  EXPECT_FALSE(device.Find("tabs"));

  // The page never learns a token.
  OpenSettings();
  ASSERT_TRUE(WaitForPageText("account-state", "Signed in as"));
  const std::string page =
      content::EvalJs(active_contents(), "document.documentElement.outerHTML")
          .ExtractString();
  EXPECT_EQ(page.find("access-1"), std::string::npos);
  EXPECT_EQ(page.find("refresh-1"), std::string::npos);
}

// Tabs opened, navigated and closed here change the shared rows.
IN_PROC_BROWSER_TEST_F(AvoraTabSyncBrowserTest, LocalChangesGoUp) {
  SignInThroughSettings();

  const GURL opened = TestURL("opened.example");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), opened, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  std::optional<base::DictValue> row = WaitForOneRow(opened);
  ASSERT_TRUE(row);
  const std::string id = *row->FindString("id");

  // Navigated: the same row follows.
  const GURL moved = TestURL("moved.example");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), moved));
  ASSERT_TRUE(base::test::RunUntil([&] {
    std::optional<base::DictValue> now = supabase_->Row("items", id);
    return now && *now->FindString("url") == moved.spec();
  }));

  // Closed: tombstoned, not deleted.
  TabStripModel* model = browser()->GetTabStripModel();
  model->CloseWebContentsAt(model->GetIndexOfWebContents(active_contents()),
                            TabCloseTypes::CLOSE_USER_GESTURE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    std::optional<base::DictValue> now = supabase_->Row("items", id);
    return now && !FakeSupabase::IsLive(*now);
  }));
}

// Tabs the phone opens, navigates and closes show up here; pinned items too.
IN_PROC_BROWSER_TEST_F(AvoraTabSyncBrowserTest, RemoteChangesComeDown) {
  SignInThroughSettings();
  const std::string space = LocalSpaceId();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !!supabase_->Row("spaces", space); }));

  // The phone opens a tab: it appears here, unloaded, under the same id.
  const std::string id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  const GURL from_phone = TestURL("phone.example");
  base::DictValue open;
  open.Set("id", id);
  open.Set("space_id", space);
  open.Set("kind", "today");
  open.Set("url", from_phone.spec());
  open.Set("title", "From the phone");
  open.Set("position", "zz");
  supabase_->PhoneWrites("items", std::move(open));
  ASSERT_TRUE(base::test::RunUntil([&] { return !!FindTab(from_phone); }));
  tabs::TabInterface* tab = FindTab(from_phone);
  EXPECT_EQ(GetTabGuid(tab->GetContents()), id);
  EXPECT_TRUE(tab->GetContents()->GetController().NeedsReload());
  EXPECT_EQ(tab->GetContents()->GetTitle(), u"From the phone");

  // The phone navigates it: the copy here follows without loading.
  const GURL elsewhere = TestURL("elsewhere.example");
  base::DictValue navigate;
  navigate.Set("id", id);
  navigate.Set("url", elsewhere.spec());
  navigate.Set("title", "Elsewhere");
  supabase_->PhoneWrites("items", std::move(navigate));
  ASSERT_TRUE(base::test::RunUntil([&] { return !!FindTab(elsewhere); }));
  EXPECT_FALSE(FindTab(from_phone));
  EXPECT_TRUE(FindTab(elsewhere)->GetContents()->GetController().NeedsReload());

  // The phone closes it: it closes here, and isn't sent back up as new.
  base::DictValue close;
  close.Set("id", id);
  close.Set("deleted_at", "2026-10-28T00:00:00+00:00");
  supabase_->PhoneWrites("items", std::move(close));
  ASSERT_TRUE(base::test::RunUntil([&] { return !FindTab(elsewhere); }));
  Wait(kPull * 3);
  EXPECT_FALSE(FakeSupabase::IsLive(*supabase_->Row("items", id)));
  EXPECT_FALSE(FindTab(elsewhere));

  // The phone pins a page: it becomes a pinned item in this Space.
  const GURL pinned = TestURL("pinned.example");
  base::DictValue pin;
  pin.Set("id", base::Uuid::GenerateRandomV4().AsLowercaseString());
  pin.Set("space_id", space);
  pin.Set("kind", "pinned");
  pin.Set("url", pinned.spec());
  pin.Set("title", "Pinned on the phone");
  pin.Set("position", "a0");
  supabase_->PhoneWrites("items", std::move(pin));
  ASSERT_TRUE(base::test::RunUntil([&] {
    const SidebarItemStore store(prefs());
    for (const SidebarItem& item :
         store.GetItems(space, SidebarItemType::kPinned)) {
      if (item.url == pinned.spec()) {
        return true;
      }
    }
    return false;
  }));
}

// Signing in to an account that already has this Mac's Space and tab links
// them rather than duplicating them.
IN_PROC_BROWSER_TEST_F(AvoraTabSyncBrowserTest, SignInMergesMatchingRows) {
  const GURL shared = TestURL("shared.example");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), shared, WindowOpenDisposition::NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  const SpaceManager spaces(prefs());
  const std::string local_space = spaces.GetSpaces().front().id;

  const std::string account_space =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  base::DictValue space;
  space.Set("id", account_space);
  space.Set("name", spaces.GetSpaces().front().name);
  space.Set("icon", "");
  space.Set("color", "");
  space.Set("position", "a0");
  supabase_->PhoneWrites("spaces", std::move(space));
  const std::string account_tab =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  base::DictValue tab;
  tab.Set("id", account_tab);
  tab.Set("space_id", account_space);
  tab.Set("kind", "today");
  tab.Set("url", shared.spec());
  tab.Set("title", "Shared");
  tab.Set("position", "a0");
  supabase_->PhoneWrites("items", std::move(tab));

  SignInThroughSettings();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return service()->GetStatus().last_upload != base::Time();
  }));
  Wait(kPull * 3);

  // One Space and one row for the tab, both the account's.
  EXPECT_EQ(supabase_->Rows("spaces").size(), 1u);
  EXPECT_FALSE(supabase_->Row("spaces", local_space));
  std::vector<base::DictValue> rows = supabase_->LiveItemsWithUrl(shared);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(*rows[0].FindString("id"), account_tab);
  // And one tab here.
  int copies = 0;
  TabStripModel* model = browser()->GetTabStripModel();
  for (int i = 0; i < model->count(); ++i) {
    copies += model->GetWebContentsAt(i)->GetVisibleURL() == shared;
  }
  EXPECT_EQ(copies, 1);

  // A tab closed on the phone closes the linked tab here.
  base::DictValue close;
  close.Set("id", account_tab);
  close.Set("deleted_at", "2026-10-28T00:00:00+00:00");
  supabase_->PhoneWrites("items", std::move(close));
  ASSERT_TRUE(base::test::RunUntil([&] { return !FindTab(shared); }));
}

// Signing out deletes the device row and stops sharing; the tabs stay.
IN_PROC_BROWSER_TEST_F(AvoraTabSyncBrowserTest, SignOutStopsSharing) {
  SignInThroughSettings();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !supabase_->RequestsTo("POST", "/rest/v1/devices").empty();
  }));
  const std::string device_id =
      prefs()->GetString(sync::SessionStore::kDeviceIdPref);

  OpenSettings();
  ASSERT_TRUE(WaitForPageText("account-state", "Signed in as"));
  ASSERT_TRUE(content::ExecJs(
      active_contents(), "document.getElementById('account-sign-out').click()"));
  ASSERT_TRUE(WaitForPageText("account-state", "Not signed in."));

  std::vector<Request> deletes = supabase_->RequestsTo("DELETE", "/rest/v1/");
  ASSERT_EQ(deletes.size(), 1u);
  EXPECT_EQ(deletes[0].path,
            base::StrCat({"/rest/v1/devices?id=eq.", device_id}));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return supabase_->RequestsTo("POST", "/auth/v1/logout").size() == 1;
  }));
  EXPECT_EQ(service()->state(), TabSyncService::State::kSignedOut);
  EXPECT_TRUE(prefs()->GetDict(sync::SessionStore::kSessionPref).empty());

  // Signed out, tabs are not shared.
  const size_t writes =
      supabase_->RequestsTo("POST", "/rest/v1/rpc/sync_items").size();
  const GURL after = TestURL("after.example");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), after, WindowOpenDisposition::NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  Wait(kPull * 3);
  EXPECT_EQ(supabase_->RequestsTo("POST", "/rest/v1/rpc/sync_items").size(),
            writes);
  EXPECT_TRUE(supabase_->LiveItemsWithUrl(after).empty());
}

// The tab in front gets a preview a moment after it loads: a JPEG in the
// owner's folder of tab-previews, linked from its row. A page with a password
// field gets none.
IN_PROC_BROWSER_TEST_F(AvoraTabSyncBrowserTest, PreviewsSkipPrivatePages) {
  SignInThroughSettings();

  const GURL page = TestURL("preview.example");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), page, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  std::optional<base::DictValue> row = WaitForOneRow(page);
  ASSERT_TRUE(row);
  const std::string id = *row->FindString("id");
  const std::string path = base::StrCat({"user-1/", id, ".jpg"});
  ASSERT_TRUE(base::test::RunUntil([&] {
    std::optional<base::DictValue> now = supabase_->Row("items", id);
    const std::string* preview =
        now ? now->FindString("preview_path") : nullptr;
    return preview && *preview == path;
  }));
  std::vector<Request> uploads = supabase_->RequestsTo(
      "POST", base::StrCat({"/storage/v1/object/tab-previews/", path}));
  ASSERT_FALSE(uploads.empty());
  // A JPEG, and small.
  EXPECT_TRUE(uploads[0].body.starts_with("\xFF\xD8"));
  EXPECT_LE(uploads[0].body.size(), 256u * 1024u);

  const GURL secret = embedded_test_server()->GetURL(
      "secret.example", "/password/filled_simple_password_form.html");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), secret, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  std::optional<base::DictValue> secret_row = WaitForOneRow(secret);
  ASSERT_TRUE(secret_row);
  Wait(base::Seconds(4));
  const std::string secret_id = *secret_row->FindString("id");
  EXPECT_TRUE(supabase_
                  ->RequestsTo("POST",
                               base::StrCat({"/storage/v1/object/tab-previews/"
                                             "user-1/",
                                             secret_id}))
                  .empty());
  const base::Value* secret_preview =
      supabase_->Row("items", secret_id)->Find("preview_path");
  EXPECT_TRUE(!secret_preview || secret_preview->is_none());
}

// A window keeps a budget of background tabs loaded and unloads the least
// recently used of the rest, keeping their place and history.
IN_PROC_BROWSER_TEST_F(AvoraTabSyncBrowserTest, UnloadsTabsBeyondTheBudget) {
  std::vector<GURL> urls;
  for (int i = 0; i < 16; ++i) {
    urls.push_back(TestURL(base::StrCat({"warm", base::NumberToString(i),
                                         ".example"})));
    ui_test_utils::NavigateToURLWithDisposition(
        browser(), urls.back(), WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  }
  TabStripModel* model = browser()->GetTabStripModel();
  const auto loaded_background = [&] {
    int loaded = 0;
    for (int i = 0; i < model->count(); ++i) {
      content::WebContents* contents = model->GetWebContentsAt(i);
      if (contents != active_contents() &&
          !contents->GetController().NeedsReload() &&
          contents->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) {
        ++loaded;
      }
    }
    return loaded;
  };
  ASSERT_TRUE(base::test::RunUntil([&] { return loaded_background() <= 12; }));
  EXPECT_EQ(loaded_background(), 12);
  // The oldest went cold; every page is still there.
  for (const GURL& url : urls) {
    EXPECT_TRUE(FindTab(url)) << url;
  }
  EXPECT_TRUE(FindTab(urls[0])->GetContents()->GetController().NeedsReload());
  EXPECT_FALSE(
      FindTab(urls[14])->GetContents()->GetController().NeedsReload());
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
  EXPECT_TRUE(supabase_->RequestsTo("POST", "/rest/v1/rpc/").empty());
}

}  // namespace avora
