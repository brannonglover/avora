// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_tab_sync_service.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include "base/auto_reset.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"
#include "chrome/browser/avora/sync/avora_device_name.h"
#include "chrome/browser/avora/sync/avora_fractional_index.h"
#include "chrome/browser/avora/sync/avora_sync_config.h"
#include "chrome/browser/profiles/profile.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/os_crypt/common/os_crypt_switches.h"
#include "content/public/browser/storage_partition.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace avora::sync {

namespace {

constexpr char kUserDataKey[] = "avora_tab_sync_service";

// Set once from inside //chrome/browser (browser_view.cc): this target cannot
// reach the browser process or the views layer.
os_crypt_async::OSCryptAsync* g_os_crypt_async = nullptr;

// Set once from the views layer; see TodayTabs.
TabSyncService::TodayTabs* g_today_tabs = nullptr;

TabSyncService::TodayTabs* GetTodayTabs() {
  return g_today_tabs;
}

// Local changes wait this long, so a burst goes up as one request.
constexpr base::TimeDelta kDefaultPushDelay = base::Seconds(1);
// How often other devices' changes are fetched while signed in.
constexpr base::TimeDelta kDefaultPullInterval = base::Seconds(10);
// Model notifications come in bursts; one diff covers them.
constexpr base::TimeDelta kReconcileDelay = base::Milliseconds(300);
// A transaction that committed late can carry an updated_at a little older
// than one already seen; docs/SYNC.md asks for this much overlap.
constexpr base::TimeDelta kCatchUpOverlap = base::Seconds(60);
constexpr base::TimeDelta kInitialRetryDelay = base::Seconds(15);
constexpr base::TimeDelta kMaxRetryDelay = base::Minutes(10);
// docs/SYNC.md: at most one preview a minute per tab.
constexpr base::TimeDelta kPreviewInterval = base::Minutes(1);
constexpr size_t kMaxPreviewSize = 256 * 1024;
// PostgREST's default maximum page on Supabase.
constexpr size_t kPageSize = 1000;

bool IsShareableUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && url.has_host();
}

const char* CurrentPlatform() {
#if BUILDFLAG(IS_MAC)
  return "macos";
#elif BUILDFLAG(IS_WIN)
  return "windows";
#else
  return "linux";
#endif
}

std::optional<std::pair<base::TimeDelta, base::TimeDelta>>& DelaysForTesting() {
  static std::optional<std::pair<base::TimeDelta, base::TimeDelta>> delays;
  return delays;
}

// A format check only; Supabase decides whether the address is real.
bool IsPlausibleEmail(std::string_view email) {
  if (email.size() < 3 || email.size() > 254) {
    return false;
  }
  const size_t at = email.find('@');
  if (at == std::string_view::npos || at == 0 || at == email.size() - 1 ||
      email.find('@', at + 1) != std::string_view::npos) {
    return false;
  }
  for (char c : email) {
    if (c <= 0x20 || c >= 0x7f) {
      return false;
    }
  }
  return true;
}

// The digits of a pasted or typed code, which may have spaces or a dash in
// the middle. Supabase codes are 6 to 10 digits depending on project settings.
std::optional<std::string> NormalizeCode(std::string_view code) {
  std::string digits;
  for (char c : code) {
    if (base::IsAsciiDigit(c)) {
      digits.push_back(c);
    } else if (c != ' ' && c != '-') {
      return std::nullopt;
    }
  }
  if (digits.size() < 6 || digits.size() > 10) {
    return std::nullopt;
  }
  return digits;
}

TabSyncService::Result ResultForFailure(RequestStatus status) {
  switch (status) {
    case RequestStatus::kRateLimited:
      return TabSyncService::Result::kRateLimited;
    case RequestStatus::kOk:
    case RequestStatus::kNetworkError:
    case RequestStatus::kUnauthorized:
    case RequestStatus::kForbidden:
    case RequestStatus::kRejected:
      return TabSyncService::Result::kNetworkError;
  }
}

}  // namespace

// static
void TabSyncService::SetOSCryptAsync(
    os_crypt_async::OSCryptAsync* os_crypt_async) {
  g_os_crypt_async = os_crypt_async;
}

// static
void TabSyncService::SetTodayTabs(TodayTabs* today_tabs) {
  g_today_tabs = today_tabs;
}

// static
TabSyncService* TabSyncService::GetForProfile(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }
  if (TabSyncService* existing = GetForProfileIfExists(profile)) {
    return existing;
  }
  auto service = std::make_unique<TabSyncService>(profile);
  TabSyncService* raw = service.get();
  profile->SetUserData(kUserDataKey, std::move(service));
  return raw;
}

// static
TabSyncService* TabSyncService::GetForProfileIfExists(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }
  return static_cast<TabSyncService*>(profile->GetUserData(kUserDataKey));
}

// static
void TabSyncService::SetUploadDelaysForTesting(base::TimeDelta quiet_period,
                                               base::TimeDelta max_delay) {
  DelaysForTesting() = std::make_pair(quiet_period, max_delay);
}

TabSyncService::TabSyncService(Profile* profile)
    : profile_(profile),
      store_(std::make_unique<SessionStore>(
          profile->GetPrefs(),
          base::CommandLine::ForCurrentProcess()->HasSwitch(
              os_crypt::switches::kUseMockKeychain)
              ? SessionStore::Environment::kMockKeychain
              : SessionStore::Environment::kSecure)),
      client_(std::make_unique<SupabaseClient>(
          profile->GetDefaultStoragePartition()
              ->GetURLLoaderFactoryForBrowserProcess(),
          kSupabaseUrl,
          kSupabasePublishableKey)),
      shared_(std::make_unique<SharedStore>(profile->GetPrefs())),
      push_delay_(DelaysForTesting() ? DelaysForTesting()->first
                                     : kDefaultPushDelay),
      pull_interval_(DelaysForTesting() ? DelaysForTesting()->second
                                        : kDefaultPullInterval) {
  models_ = std::make_unique<SharedModels>(
      profile->GetPrefs(),
      base::BindRepeating(&TabSyncService::OnModelsChanged,
                          base::Unretained(this)));
  models_->set_pinned_item_remover(base::BindRepeating(
      [](Profile* profile, const std::string& item_id) {
        if (TodayTabs* today_tabs = GetTodayTabs()) {
          today_tabs->RemovePinnedItem(profile, item_id);
        }
      },
      profile));

  // Shown before sign-in completes, so the email reads as the stored one.
  pending_email_ = store_->GetEmail();

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&GetDeviceNameBlocking),
      base::BindOnce(&TabSyncService::OnDeviceName,
                     weak_factory_.GetWeakPtr()));
  if (g_os_crypt_async) {
    g_os_crypt_async->GetInstance(base::BindOnce(
        &TabSyncService::OnEncryptorReady, weak_factory_.GetWeakPtr()));
  }
}

TabSyncService::~TabSyncService() = default;

void TabSyncService::OnEncryptorReady(
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  store_->SetEncryptor(std::move(encryptor));
  if (state_ == State::kSignedOut && store_->HasStoredSession()) {
    if (std::optional<AuthSession> session = store_->Load()) {
      session_ = std::move(session);
      state_ = State::kSignedIn;
      StartSync(/*fresh_sign_in=*/false);
    } else {
      // Stored here by another Mac, or the Keychain item was reset.
      store_->Clear();
      SetResult(Result::kSessionExpired);
    }
  }
  NotifyChanged();
}

void TabSyncService::OnDeviceName(std::string name) {
  device_name_ = std::move(name);
  UpsertDeviceRow();
  NotifyChanged();
}

// ── Sign-in ─────────────────────────────────────────────────────────────────

void TabSyncService::RequestCode(const std::string& email) {
  if (busy_ || state_ == State::kSignedIn) {
    return;
  }
  const std::string trimmed =
      std::string(base::TrimWhitespaceASCII(email, base::TRIM_ALL));
  if (!IsPlausibleEmail(trimmed)) {
    SetResult(Result::kInvalidEmail);
    NotifyChanged();
    return;
  }
  busy_ = true;
  NotifyChanged();
  client_->RequestEmailCode(
      trimmed, base::BindOnce(&TabSyncService::OnCodeRequested,
                              weak_factory_.GetWeakPtr(), trimmed));
}

void TabSyncService::OnCodeRequested(std::string email, RequestStatus status) {
  busy_ = false;
  if (status == RequestStatus::kOk) {
    pending_email_ = std::move(email);
    state_ = State::kAwaitingCode;
    SetResult(Result::kCodeSent);
  } else if (status == RequestStatus::kRejected ||
             status == RequestStatus::kForbidden) {
    SetResult(Result::kInvalidEmail);
  } else {
    SetResult(ResultForFailure(status));
  }
  NotifyChanged();
}

void TabSyncService::VerifyCode(const std::string& code) {
  if (busy_ || state_ != State::kAwaitingCode) {
    return;
  }
  std::optional<std::string> digits = NormalizeCode(code);
  if (!digits) {
    SetResult(Result::kInvalidCode);
    NotifyChanged();
    return;
  }
  busy_ = true;
  NotifyChanged();
  client_->VerifyEmailCode(pending_email_, *digits,
                           base::BindOnce(&TabSyncService::OnCodeVerified,
                                          weak_factory_.GetWeakPtr()));
}

void TabSyncService::OnCodeVerified(RequestStatus status,
                                    std::optional<AuthSession> session) {
  busy_ = false;
  if (status == RequestStatus::kOk && session) {
    if (session->email.empty()) {
      session->email = pending_email_;
    }
    StartSession(std::move(*session));
    SetResult(Result::kSignedIn);
  } else if (status == RequestStatus::kForbidden ||
             status == RequestStatus::kRejected ||
             status == RequestStatus::kUnauthorized) {
    // Supabase answers a wrong or expired code with 403 otp_expired.
    SetResult(Result::kWrongCode);
  } else {
    SetResult(ResultForFailure(status));
  }
  NotifyChanged();
}

void TabSyncService::CancelSignIn() {
  if (state_ != State::kAwaitingCode || busy_) {
    return;
  }
  state_ = State::kSignedOut;
  SetResult(Result::kNone);
  NotifyChanged();
}

void TabSyncService::StartSession(AuthSession session) {
  memory_only_ = store_->Save(session) == SessionStore::SaveResult::kMemoryOnly;
  pending_email_ = session.email;
  session_ = std::move(session);
  state_ = State::kSignedIn;
  upload_failing_ = false;
  StartSync(/*fresh_sign_in=*/true);
}

// ── Tokens ──────────────────────────────────────────────────────────────────

void TabSyncService::WithFreshToken(TokenCallback callback) {
  if (!session_) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  if (!session_->NeedsRefresh(base::Time::Now())) {
    std::move(callback).Run(session_->access_token);
    return;
  }
  token_waiters_.push_back(std::move(callback));
  if (refresh_in_flight_) {
    return;
  }
  refresh_in_flight_ = true;
  client_->RefreshSession(session_->refresh_token,
                          base::BindOnce(&TabSyncService::OnRefreshed,
                                         weak_factory_.GetWeakPtr()));
}

void TabSyncService::OnRefreshed(RequestStatus status,
                                 std::optional<AuthSession> refreshed) {
  refresh_in_flight_ = false;
  std::vector<TokenCallback> waiters = std::move(token_waiters_);
  std::optional<std::string> token;
  if (status == RequestStatus::kOk && refreshed && session_) {
    // The refresh response repeats the user, but keep what is known if not.
    if (refreshed->email.empty()) {
      refreshed->email = session_->email;
    }
    if (refreshed->user_id.empty()) {
      refreshed->user_id = session_->user_id;
    }
    memory_only_ =
        store_->Save(*refreshed) == SessionStore::SaveResult::kMemoryOnly;
    session_ = std::move(refreshed);
    token = session_->access_token;
  }
  const bool refused = status == RequestStatus::kRejected ||
                       status == RequestStatus::kForbidden ||
                       status == RequestStatus::kUnauthorized;
  for (TokenCallback& waiter : waiters) {
    std::move(waiter).Run(token);
  }
  if (refused && session_) {
    ExpireSession();
  }
}

void TabSyncService::ExpireSession() {
  StopSync();
  // Changes made here from now on aren't recorded, so the next sign-in
  // merges from scratch.
  shared_->Reset(std::string());
  session_.reset();
  store_->Clear();
  state_ = State::kSignedOut;
  SetResult(Result::kSessionExpired);
  NotifyChanged();
}

// ── Shared rows ─────────────────────────────────────────────────────────────

bool TabSyncService::IsSyncing() const {
  return state_ == State::kSignedIn && session_ && !signing_out_;
}

void TabSyncService::StartSync(bool fresh_sign_in) {
  if (!session_) {
    return;
  }
  // A new sign-in always merges from scratch: changes made here while signed
  // out were never recorded, so a stored baseline would miss them.
  if (fresh_sign_in || shared_->user_id() != session_->user_id) {
    shared_->Reset(session_->user_id);
  }
  device_row_written_ = false;
  failures_ = 0;
  UpsertDeviceRow();
  if (shared_->loaded()) {
    RebaseModels();
  }
  pull_timer_.Start(FROM_HERE, pull_interval_,
                    base::BindRepeating(&TabSyncService::Pull,
                                        base::Unretained(this)));
  Pull();
}

void TabSyncService::StopSync() {
  pull_timer_.Stop();
  push_timer_.Stop();
  reconcile_timer_.Stop();
  retry_timer_.Stop();
  deferred_today_.clear();
}

void TabSyncService::UpsertDeviceRow() {
  if (!IsSyncing() || device_name_.empty() || device_row_written_) {
    return;
  }
  WithFreshToken(base::BindOnce(
      [](base::WeakPtr<TabSyncService> self,
         std::optional<std::string> access_token) {
        if (!self || !access_token || !self->IsSyncing()) {
          return;
        }
        base::DictValue body;
        body.Set("id", self->store_->GetOrCreateDeviceId());
        body.Set("name", self->device_name_);
        body.Set("platform", CurrentPlatform());
        body.Set("last_seen_at", "now");
        self->client_->UpsertDevice(
            *access_token, base::WriteJson(body).value_or(std::string()),
            base::BindOnce(
                [](base::WeakPtr<TabSyncService> self, RequestStatus status) {
                  if (!self) {
                    return;
                  }
                  if (status == RequestStatus::kOk) {
                    self->device_row_written_ = true;
                  } else if (status == RequestStatus::kForbidden) {
                    // The id belongs to another account (its sign-out
                    // couldn't delete the row): take a new one.
                    self->store_->RotateDeviceId();
                  }
                },
                self));
      },
      weak_factory_.GetWeakPtr()));
}

void TabSyncService::SyncSoon() {
  if (IsSyncing()) {
    Pull();
  }
}

void TabSyncService::Pull() {
  if (!IsSyncing() || pulling_) {
    return;
  }
  TodayTabs* today_tabs = GetTodayTabs();
  const bool full = !shared_->loaded();
  // The first load links this device's open tabs to the account's, which
  // needs every restored tab in place.
  if (full && today_tabs && !today_tabs->IsReady(profile_)) {
    return;
  }
  RetryDeferredToday();
  UpsertDeviceRow();

  std::optional<std::string> since;
  if (!full && shared_->cursor()) {
    since = FormatServerTime(*shared_->cursor() - kCatchUpOverlap);
  }
  pulling_ = true;
  FetchTable(SupabaseClient::Table::kSpaces, since, 0, base::ListValue(),
             base::BindOnce(&TabSyncService::OnSpacesPulled,
                            weak_factory_.GetWeakPtr(), full),
             std::nullopt);
}

void TabSyncService::FetchTable(
    SupabaseClient::Table table,
    std::optional<std::string> since,
    size_t offset,
    base::ListValue rows,
    base::OnceCallback<void(bool, base::ListValue)> done,
    std::optional<std::string> access_token) {
  if (!access_token) {
    // First call for a page: get a token, then come back.
    WithFreshToken(base::BindOnce(
        [](base::WeakPtr<TabSyncService> self, SupabaseClient::Table table,
           std::optional<std::string> since, size_t offset,
           base::ListValue rows,
           base::OnceCallback<void(bool, base::ListValue)> done,
           std::optional<std::string> token) {
          if (!self) {
            return;
          }
          if (!token) {
            std::move(done).Run(false, base::ListValue());
            return;
          }
          self->FetchTable(table, std::move(since), offset, std::move(rows),
                           std::move(done), std::move(token));
        },
        weak_factory_.GetWeakPtr(), table, std::move(since), offset,
        std::move(rows), std::move(done)));
    return;
  }
  const std::optional<std::string_view> since_view =
      since ? std::optional<std::string_view>(*since) : std::nullopt;
  client_->FetchRows(
      *access_token, table, since_view, offset, kPageSize,
      base::BindOnce(&TabSyncService::OnTableFetched,
                     weak_factory_.GetWeakPtr(), table, since, offset,
                     std::move(rows), std::move(done)));
}

void TabSyncService::OnTableFetched(
    SupabaseClient::Table table,
    std::optional<std::string> since,
    size_t offset,
    base::ListValue rows,
    base::OnceCallback<void(bool, base::ListValue)> done,
    RequestStatus status,
    std::optional<base::ListValue> page) {
  if (status != RequestStatus::kOk || !page) {
    if (status == RequestStatus::kUnauthorized && session_) {
      // Revoked or expired early: the next attempt refreshes first.
      session_->expires_at = base::Time();
    }
    std::move(done).Run(false, base::ListValue());
    return;
  }
  const size_t count = page->size();
  for (base::Value& row : *page) {
    rows.Append(std::move(row));
  }
  if (count < kPageSize) {
    std::move(done).Run(true, std::move(rows));
    return;
  }
  FetchTable(table, std::move(since), offset + count, std::move(rows),
             std::move(done), std::nullopt);
}

void TabSyncService::OnSpacesPulled(bool full,
                                    bool ok,
                                    base::ListValue spaces) {
  if (!ok || !IsSyncing()) {
    pulling_ = false;
    if (IsSyncing()) {
      OnSyncFailed();
    }
    return;
  }
  std::optional<std::string> since;
  if (!full && shared_->cursor()) {
    since = FormatServerTime(*shared_->cursor() - kCatchUpOverlap);
  }
  FetchTable(SupabaseClient::Table::kItems, since, 0, base::ListValue(),
             base::BindOnce(&TabSyncService::OnPulled,
                            weak_factory_.GetWeakPtr(), full,
                            std::move(spaces)),
             std::nullopt);
}

void TabSyncService::OnPulled(bool full,
                              base::ListValue spaces,
                              bool ok,
                              base::ListValue items) {
  pulling_ = false;
  if (!IsSyncing()) {
    return;
  }
  if (!ok) {
    OnSyncFailed();
    return;
  }
  if (!full) {
    // This device's own changes first, so applying the server's can't be
    // mistaken for them.
    ReconcileModels(/*diff_first=*/false);
  }
  std::vector<SharedRow> changed_spaces =
      shared_->MergeFromServer(SharedTable::kSpaces, spaces);
  std::vector<SharedRow> changed_items =
      shared_->MergeFromServer(SharedTable::kItems, items);

  TodayTabs* today_tabs = GetTodayTabs();
  {
    base::AutoReset<bool> applying(&applying_, true);
    if (full) {
      models_->LinkMatching(*shared_);
      // Open tabs already in the account (same Space, same URL) become those
      // rows rather than copies of them.
      if (today_tabs) {
        std::set<std::string> taken;
        for (const TodayTab& tab : today_tabs->Collect(profile_)) {
          const std::string space =
              shared_->ToShared(SharedTable::kSpaces, tab.space_id);
          for (const SharedRow* row : shared_->ItemsIn(space, kKindToday)) {
            const std::string* url = row->columns.FindString("url");
            if (!taken.count(row->id) && url && *url == tab.url.spec()) {
              shared_->Link(SharedTable::kItems, row->id, tab.guid);
              taken.insert(row->id);
              break;
            }
          }
        }
      }
    }
    models_->Apply(*shared_, changed_spaces, changed_items);
  }

  std::vector<std::string> today_ids;
  for (const SharedRow& row : changed_items) {
    today_ids.push_back(row.id);
  }
  ApplyTodayRows(today_ids);

  if (full) {
    FinishFirstLoad();
  } else {
    RebaseModels();
  }
  OnSyncSucceeded();
  if (shared_->HasPending()) {
    SchedulePush(base::TimeDelta());
  }
}

void TabSyncService::FinishFirstLoad() {
  // Whatever this device has that the account doesn't goes up.
  ReconcileModels(/*diff_first=*/true);
  if (TodayTabs* today_tabs = GetTodayTabs()) {
    std::map<std::string, std::optional<std::string>> last_position;
    for (const TodayTab& tab : today_tabs->Collect(profile_)) {
      const std::string shared_id =
          shared_->ToShared(SharedTable::kItems, tab.guid);
      if (shared_->Find(SharedTable::kItems, shared_id) ||
          !IsShareableUrl(tab.url)) {
        continue;
      }
      const std::string space =
          shared_->ToShared(SharedTable::kSpaces, tab.space_id);
      if (!last_position.count(space)) {
        std::vector<const SharedRow*> rows =
            shared_->ItemsIn(space, kKindToday);
        const std::string* position =
            rows.empty() ? nullptr : rows.back()->columns.FindString("position");
        last_position[space] =
            position && IsValidKey(*position)
                ? std::optional<std::string>(*position)
                : std::nullopt;
      }
      std::optional<std::string>& after = last_position[space];
      std::optional<std::string> position = KeyBetween(
          after ? std::optional<std::string_view>(*after) : std::nullopt,
          std::nullopt);
      if (!position) {
        continue;
      }
      base::DictValue columns;
      columns.Set("space_id", space);
      columns.Set("kind", kKindToday);
      columns.Set("url", tab.url.spec());
      columns.Set("title", base::UTF16ToUTF8(tab.title));
      columns.Set("position", *position);
      shared_->Write(SharedTable::kItems, shared_id, std::move(columns));
      after = std::move(position);
    }
  }
  shared_->set_loaded();
  shared_->SaveNow();
}

void TabSyncService::ReconcileModels(bool diff_first) {
  if (!IsSyncing()) {
    return;
  }
  std::set<std::string> live_spaces;
  for (const SharedRow* row : shared_->Rows(SharedTable::kSpaces)) {
    if (!row->deleted) {
      live_spaces.insert(row->id);
    }
  }
  shared_->Reconcile(SharedTable::kSpaces, SharedModels::kSpacesScope,
                     models_->ProjectSpaces(), diff_first);
  shared_->Reconcile(SharedTable::kItems, SharedModels::kSidebarScope,
                     models_->ProjectSidebar(), diff_first);

  // A Space deleted here takes its rows with it, today tabs included: they
  // are hidden here and would otherwise linger on every other device.
  for (const std::string& space : live_spaces) {
    const SharedRow* row = shared_->Find(SharedTable::kSpaces, space);
    if (!row || !row->deleted) {
      continue;
    }
    std::vector<std::string> doomed;
    for (const SharedRow* item : shared_->Rows(SharedTable::kItems)) {
      const std::string* item_space = item->columns.FindString("space_id");
      if (!item->deleted && item_space && *item_space == space) {
        doomed.push_back(item->id);
      }
    }
    for (const std::string& id : doomed) {
      shared_->Delete(SharedTable::kItems, id);
    }
  }
}

void TabSyncService::RebaseModels() {
  shared_->Rebase(SharedTable::kSpaces, SharedModels::kSpacesScope,
                  models_->ProjectSpaces());
  shared_->Rebase(SharedTable::kItems, SharedModels::kSidebarScope,
                  models_->ProjectSidebar());
}

void TabSyncService::OnModelsChanged() {
  if (applying_ || !IsSyncing() || !shared_->loaded()) {
    return;
  }
  // Batches the burst of notifications one user action makes.
  reconcile_timer_.Start(
      FROM_HERE, kReconcileDelay,
      base::BindOnce(
          [](TabSyncService* self) {
            self->ReconcileModels(/*diff_first=*/false);
            if (self->shared_->HasPending()) {
              self->SchedulePush(self->push_delay_);
            }
          },
          base::Unretained(this)));
}

// ── Today tabs ──────────────────────────────────────────────────────────────

void TabSyncService::ApplyTodayRows(const std::vector<std::string>& shared_ids) {
  TodayTabs* today_tabs = GetTodayTabs();
  if (!today_tabs || shared_ids.empty()) {
    return;
  }
  if (!today_tabs->IsReady(profile_)) {
    for (const std::string& id : shared_ids) {
      if (!std::ranges::contains(deferred_today_, id)) {
        deferred_today_.push_back(id);
      }
    }
    return;
  }
  for (const std::string& id : shared_ids) {
    const SharedRow* row = shared_->Find(SharedTable::kItems, id);
    if (!row) {
      continue;
    }
    const std::string guid = shared_->ToLocal(SharedTable::kItems, id);
    const std::string* kind = row->columns.FindString("kind");
    if (row->deleted || !kind || *kind != kKindToday) {
      today_tabs->Remove(profile_, guid);
      continue;
    }
    const std::string* shared_space = row->columns.FindString("space_id");
    const std::string* url = row->columns.FindString("url");
    const std::string* title = row->columns.FindString("title");
    const std::string space =
        shared_space ? models_->LocalSpaceId(*shared_, *shared_space)
                     : std::string();
    if (space.empty() || !url || !IsShareableUrl(GURL(*url))) {
      continue;
    }
    std::vector<std::string> before;
    for (const SharedRow* other : shared_->ItemsIn(*shared_space, kKindToday)) {
      if (other->id == id) {
        break;
      }
      before.push_back(shared_->ToLocal(SharedTable::kItems, other->id));
    }
    TodayTab tab;
    tab.guid = guid;
    tab.space_id = space;
    tab.url = GURL(*url);
    tab.title = base::UTF8ToUTF16(title ? *title : std::string());
    today_tabs->Upsert(profile_, tab, before);
  }
}

void TabSyncService::RetryDeferredToday() {
  TodayTabs* today_tabs = GetTodayTabs();
  if (deferred_today_.empty() || !today_tabs ||
      !today_tabs->IsReady(profile_)) {
    return;
  }
  std::vector<std::string> ids = std::move(deferred_today_);
  deferred_today_.clear();
  ApplyTodayRows(ids);
}

std::string TabSyncService::PositionBetween(
    const std::string& shared_space,
    const std::string& predecessor_guid,
    const std::string& successor_guid) const {
  const std::vector<const SharedRow*> rows =
      shared_->ItemsIn(shared_space, kKindToday);
  const auto index_of = [&](const std::string& guid) -> std::optional<size_t> {
    if (guid.empty()) {
      return std::nullopt;
    }
    const std::string id = shared_->ToShared(SharedTable::kItems, guid);
    for (size_t i = 0; i < rows.size(); ++i) {
      if (rows[i]->id == id) {
        return i;
      }
    }
    return std::nullopt;
  };
  const auto position_at = [&](std::optional<size_t> index)
      -> std::optional<std::string_view> {
    if (!index || *index >= rows.size()) {
      return std::nullopt;
    }
    const std::string* position = rows[*index]->columns.FindString("position");
    if (!position || !IsValidKey(*position)) {
      return std::nullopt;
    }
    return std::string_view(*position);
  };

  std::optional<size_t> before = index_of(predecessor_guid);
  std::optional<size_t> after = index_of(successor_guid);
  if (before && !after) {
    after = *before + 1;
  } else if (after && !before && *after > 0) {
    before = *after - 1;
  } else if (!before && !after && !rows.empty()) {
    before = rows.size() - 1;
  }
  if (std::optional<std::string> key =
          KeyBetween(position_at(before), position_at(after))) {
    return *key;
  }
  // Inconsistent neighbours: the end of the list is always valid.
  const std::optional<std::string_view> last =
      rows.empty() ? std::nullopt : position_at(rows.size() - 1);
  return KeyBetween(last, std::nullopt).value_or("a0");
}

void TabSyncService::OnTodayTabOpened(const TodayTab& tab,
                                      const std::string& predecessor,
                                      const std::string& successor) {
  if (!IsSyncing() || !shared_->loaded() || !IsShareableUrl(tab.url)) {
    return;
  }
  const std::string space =
      shared_->ToShared(SharedTable::kSpaces, tab.space_id);
  const std::string id = shared_->ToShared(SharedTable::kItems, tab.guid);
  const SharedRow* row = shared_->Find(SharedTable::kItems, id);
  base::DictValue changes;
  if (row && row->deleted) {
    // Closed on another device while it was open here (this device hadn't
    // heard yet, e.g. it was asleep): that close wins. Reopening a tab this
    // device closed itself brings it back.
    const bool closed_here =
        row->updated_by == store_->GetOrCreateDeviceId() ||
        shared_->HasUnsentChange(SharedTable::kItems, id);
    if (!closed_here) {
      if (TodayTabs* today_tabs = GetTodayTabs()) {
        today_tabs->Remove(profile_, tab.guid);
      }
      return;
    }
    changes.Set("deleted_at", base::Value());
  }
  if (!row || row->deleted) {
    changes.Set("space_id", space);
    changes.Set("kind", kKindToday);
    changes.Set("url", tab.url.spec());
    changes.Set("title", base::UTF16ToUTF8(tab.title));
    changes.Set("position", PositionBetween(space, predecessor, successor));
  } else {
    // Already shared: only a move matters. The URL and title come from
    // navigations, never from a tab being restored or copied.
    const std::string* kind = row->columns.FindString("kind");
    const std::string* row_space = row->columns.FindString("space_id");
    if (!kind || *kind != kKindToday || !row_space || *row_space != space) {
      changes.Set("space_id", space);
      changes.Set("kind", kKindToday);
      changes.Set("position", PositionBetween(space, predecessor, successor));
    }
  }
  shared_->Write(SharedTable::kItems, id, std::move(changes));
  SchedulePush(push_delay_);
}

void TabSyncService::OnTodayTabNavigated(const TodayTab& tab) {
  if (!IsSyncing() || !shared_->loaded() || !IsShareableUrl(tab.url)) {
    return;
  }
  const std::string id = shared_->ToShared(SharedTable::kItems, tab.guid);
  const SharedRow* row = shared_->Find(SharedTable::kItems, id);
  if (!row || row->deleted) {
    // A new tab that only now reached a shareable page.
    OnTodayTabOpened(tab, std::string(), std::string());
    return;
  }
  base::DictValue changes;
  const std::string url = tab.url.spec();
  const std::string title = base::UTF16ToUTF8(tab.title);
  if (const std::string* row_url = row->columns.FindString("url");
      !row_url || *row_url != url) {
    changes.Set("url", url);
  }
  if (const std::string* row_title = row->columns.FindString("title");
      !row_title || *row_title != title) {
    changes.Set("title", title);
  }
  if (!changes.empty()) {
    changes.Set("last_active_at", FormatServerTime(base::Time::Now()));
    shared_->Write(SharedTable::kItems, id, std::move(changes));
    SchedulePush(push_delay_);
  }
}

void TabSyncService::OnTodayTabClosed(const std::string& guid) {
  if (!IsSyncing() || !shared_->loaded()) {
    return;
  }
  const std::string id = shared_->ToShared(SharedTable::kItems, guid);
  const SharedRow* row = shared_->Find(SharedTable::kItems, id);
  const std::string* kind = row ? row->columns.FindString("kind") : nullptr;
  if (!row || row->deleted || !kind || *kind != kKindToday) {
    return;
  }
  DeletePreviewOf(id);
  shared_->Delete(SharedTable::kItems, id);
  SchedulePush(push_delay_);
}

std::vector<TabSyncService::TodayTab> TabSyncService::TodayTabsForSpace(
    const std::string& space_id) const {
  std::vector<TodayTab> tabs;
  if (!IsSyncing() || !shared_->loaded()) {
    return tabs;
  }
  for (const SharedRow* row : shared_->ItemsIn(
           shared_->ToShared(SharedTable::kSpaces, space_id), kKindToday)) {
    const std::string* url = row->columns.FindString("url");
    const std::string* title = row->columns.FindString("title");
    if (!url || !IsShareableUrl(GURL(*url))) {
      continue;
    }
    TodayTab tab;
    tab.guid = shared_->ToLocal(SharedTable::kItems, row->id);
    tab.space_id = space_id;
    tab.url = GURL(*url);
    tab.title = base::UTF8ToUTF16(title ? *title : std::string());
    tabs.push_back(std::move(tab));
  }
  return tabs;
}

// ── Previews ────────────────────────────────────────────────────────────────

// static
void TabSyncService::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kPreviewsEnabledPref, true);
}

bool TabSyncService::previews_enabled() const {
  return profile_->GetPrefs()->GetBoolean(kPreviewsEnabledPref);
}

void TabSyncService::SetPreviewsEnabled(bool enabled) {
  profile_->GetPrefs()->SetBoolean(kPreviewsEnabledPref, enabled);
  if (!enabled && IsSyncing() && shared_->loaded()) {
    std::vector<std::string> with_previews;
    for (const SharedRow* row : shared_->Rows(SharedTable::kItems)) {
      const std::string* path = row->columns.FindString("preview_path");
      if (!row->deleted && path && !path->empty()) {
        with_previews.push_back(row->id);
      }
    }
    for (const std::string& id : with_previews) {
      DeletePreviewOf(id);
    }
    SchedulePush(push_delay_);
  }
  NotifyChanged();
}

bool TabSyncService::WantsPreview(const std::string& guid) const {
  if (!IsSyncing() || !shared_->loaded() || !previews_enabled()) {
    return false;
  }
  const SharedRow* row = shared_->Find(
      SharedTable::kItems, shared_->ToShared(SharedTable::kItems, guid));
  const std::string* kind = row ? row->columns.FindString("kind") : nullptr;
  if (!row || row->deleted || !kind || *kind != kKindToday) {
    return false;
  }
  const auto last = last_preview_.find(guid);
  return last == last_preview_.end() ||
         base::TimeTicks::Now() - last->second >= kPreviewInterval;
}

void TabSyncService::OnPreviewCaptured(const std::string& guid,
                                       std::string jpeg) {
  if (!WantsPreview(guid) || jpeg.empty() || jpeg.size() > kMaxPreviewSize) {
    return;
  }
  last_preview_[guid] = base::TimeTicks::Now();
  const std::string id = shared_->ToShared(SharedTable::kItems, guid);
  const std::string path = session_->user_id + "/" + id + ".jpg";
  WithFreshToken(base::BindOnce(
      [](base::WeakPtr<TabSyncService> self, std::string id, std::string path,
         std::string jpeg, std::optional<std::string> token) {
        if (!self || !token || !self->IsSyncing()) {
          return;
        }
        self->client_->UploadPreview(
            *token, path, std::move(jpeg),
            base::BindOnce(
                [](base::WeakPtr<TabSyncService> self, std::string id,
                   std::string path, RequestStatus status) {
                  if (!self || status != RequestStatus::kOk ||
                      !self->IsSyncing()) {
                    return;
                  }
                  const SharedRow* row =
                      self->shared_->Find(SharedTable::kItems, id);
                  if (!row || row->deleted) {
                    // Closed while uploading.
                    self->DeletePreviewOf(id);
                    return;
                  }
                  base::DictValue columns;
                  columns.Set("preview_path", path);
                  columns.Set("preview_updated_at",
                              FormatServerTime(base::Time::Now()));
                  self->shared_->Write(SharedTable::kItems, id,
                                       std::move(columns));
                  self->SchedulePush(self->push_delay_);
                },
                self, std::move(id), std::move(path)));
      },
      weak_factory_.GetWeakPtr(), id, path, std::move(jpeg)));
}

void TabSyncService::OnPreviewNotAllowed(const std::string& guid) {
  if (!IsSyncing() || !shared_->loaded()) {
    return;
  }
  last_preview_[guid] = base::TimeTicks::Now();
  DeletePreviewOf(shared_->ToShared(SharedTable::kItems, guid));
  SchedulePush(push_delay_);
}

void TabSyncService::DeletePreviewOf(const std::string& shared_id) {
  const SharedRow* row = shared_->Find(SharedTable::kItems, shared_id);
  const std::string* path =
      row ? row->columns.FindString("preview_path") : nullptr;
  if (!path || path->empty()) {
    return;
  }
  WithFreshToken(base::BindOnce(
      [](base::WeakPtr<TabSyncService> self, std::string path,
         std::optional<std::string> token) {
        if (self && token) {
          self->client_->DeletePreview(*token, path, base::DoNothing());
        }
      },
      weak_factory_.GetWeakPtr(), *path));
  base::DictValue columns;
  columns.Set("preview_path", base::Value());
  columns.Set("preview_updated_at", base::Value());
  shared_->Write(SharedTable::kItems, shared_id, std::move(columns));
}

// ── Pushing ─────────────────────────────────────────────────────────────────

void TabSyncService::SchedulePush(base::TimeDelta delay) {
  if (!IsSyncing() || retry_timer_.IsRunning()) {
    // While backing off, the retry sends everything.
    return;
  }
  if (!push_timer_.IsRunning() || delay < push_timer_.GetCurrentDelay()) {
    push_timer_.Start(FROM_HERE, delay,
                      base::BindOnce(&TabSyncService::Push,
                                     base::Unretained(this)));
  }
}

void TabSyncService::Push() {
  if (pushing_) {
    push_again_ = true;
    return;
  }
  if (!IsSyncing() || !shared_->HasPending()) {
    return;
  }
  pushing_ = true;
  WithFreshToken(base::BindOnce(&TabSyncService::SendPush,
                                weak_factory_.GetWeakPtr(),
                                /*retried=*/false));
}

void TabSyncService::SendPush(bool retried,
                              std::optional<std::string> access_token) {
  if (!access_token) {
    FinishPushing(false);
    return;
  }
  // Spaces first: items refer to them.
  PushTable(SupabaseClient::Table::kSpaces, retried, std::move(*access_token));
}

void TabSyncService::PushTable(SupabaseClient::Table table,
                               bool retried,
                               std::string access_token) {
  const SharedTable shared_table = table == SupabaseClient::Table::kSpaces
                                       ? SharedTable::kSpaces
                                       : SharedTable::kItems;
  base::ListValue changes =
      shared_->TakePending(shared_table, store_->GetOrCreateDeviceId());
  if (changes.empty()) {
    shared_->FinishPush(shared_table, true);
    if (table == SupabaseClient::Table::kSpaces) {
      PushTable(SupabaseClient::Table::kItems, retried, std::move(access_token));
    } else {
      FinishPushing(true);
    }
    return;
  }
  const std::string token = access_token;
  client_->SyncRows(token, table, std::move(changes),
                    base::BindOnce(&TabSyncService::OnPushed,
                                   weak_factory_.GetWeakPtr(), table, retried,
                                   std::move(access_token)));
}

void TabSyncService::OnPushed(SupabaseClient::Table table,
                              bool retried,
                              std::string access_token,
                              RequestStatus status) {
  const SharedTable shared_table = table == SupabaseClient::Table::kSpaces
                                       ? SharedTable::kSpaces
                                       : SharedTable::kItems;
  shared_->FinishPush(shared_table, status == RequestStatus::kOk);
  if (status == RequestStatus::kUnauthorized && !retried && session_) {
    // Revoked or expired early: refresh once and send everything again.
    session_->expires_at = base::Time();
    WithFreshToken(base::BindOnce(&TabSyncService::SendPush,
                                  weak_factory_.GetWeakPtr(),
                                  /*retried=*/true));
    return;
  }
  if (status != RequestStatus::kOk) {
    FinishPushing(false);
    return;
  }
  if (table == SupabaseClient::Table::kSpaces) {
    PushTable(SupabaseClient::Table::kItems, retried, std::move(access_token));
    return;
  }
  FinishPushing(true);
}

void TabSyncService::FinishPushing(bool ok) {
  pushing_ = false;
  if (signing_out_) {
    ContinueSignOut();
    return;
  }
  if (!IsSyncing()) {
    return;
  }
  if (!ok) {
    OnSyncFailed();
    return;
  }
  OnSyncSucceeded();
  if (push_again_ || shared_->HasPending()) {
    push_again_ = false;
    SchedulePush(push_delay_);
  }
}

void TabSyncService::OnSyncFailed() {
  ++failures_;
  upload_failing_ = true;
  const base::TimeDelta delay = std::min(
      kInitialRetryDelay * (1 << std::min(failures_ - 1, 10)), kMaxRetryDelay);
  push_timer_.Stop();
  retry_timer_.Start(FROM_HERE, delay,
                     base::BindOnce(
                         [](TabSyncService* self) {
                           self->Push();
                           self->Pull();
                         },
                         base::Unretained(this)));
  NotifyChanged();
}

void TabSyncService::OnSyncSucceeded() {
  failures_ = 0;
  upload_failing_ = false;
  last_upload_ = base::Time::Now();
  size_t count = 0;
  for (const SharedRow* row : shared_->Rows(SharedTable::kItems)) {
    const std::string* kind = row->columns.FindString("kind");
    if (!row->deleted && kind && *kind != kKindFolder) {
      ++count;
    }
  }
  uploaded_tab_count_ = count;
  NotifyChanged();
}

// ── Sign-out ────────────────────────────────────────────────────────────────

void TabSyncService::SignOut() {
  if (busy_) {
    return;
  }
  if (state_ != State::kSignedIn || !session_) {
    FinishSignOut(Result::kSignedOut);
    return;
  }
  busy_ = true;
  signing_out_ = true;
  // Nothing new goes up from here on.
  StopSync();
  NotifyChanged();
  if (!pushing_) {
    ContinueSignOut();
  }
}

void TabSyncService::ContinueSignOut() {
  WithFreshToken(base::BindOnce(&TabSyncService::DeleteDeviceWithToken,
                                weak_factory_.GetWeakPtr()));
}

void TabSyncService::DeleteDeviceWithToken(
    std::optional<std::string> access_token) {
  if (!access_token) {
    // Offline, or the session was already refused: the row cannot be removed
    // from here. A new id keeps it from blocking the next account.
    store_->RotateDeviceId();
    FinishSignOut(Result::kSignedOutDeviceNotRemoved);
    return;
  }
  client_->DeleteDevice(
      *access_token, store_->GetOrCreateDeviceId(),
      base::BindOnce(&TabSyncService::OnDeviceDeleted,
                     weak_factory_.GetWeakPtr(), *access_token));
}

void TabSyncService::OnDeviceDeleted(std::string access_token,
                                     RequestStatus status) {
  Result result = Result::kSignedOut;
  if (status != RequestStatus::kOk) {
    store_->RotateDeviceId();
    result = Result::kSignedOutDeviceNotRemoved;
  }
  // Best effort: revoke the refresh token too. Its result changes nothing.
  client_->SignOut(access_token, base::DoNothing());
  FinishSignOut(result);
}

void TabSyncService::FinishSignOut(Result result) {
  busy_ = false;
  signing_out_ = false;
  StopSync();
  // The account's rows are forgotten here; the tabs stay open.
  shared_->Reset(std::string());
  session_.reset();
  memory_only_ = false;
  store_->Clear();
  pending_email_.clear();
  state_ = State::kSignedOut;
  last_upload_ = base::Time();
  uploaded_tab_count_ = 0;
  upload_failing_ = false;
  SetResult(result);
  NotifyChanged();
}

// ── Status ──────────────────────────────────────────────────────────────────

TabSyncService::Status TabSyncService::GetStatus() const {
  Status status;
  status.state = state_;
  status.starting = state_ == State::kSignedOut && !store_->is_ready() &&
                    store_->HasStoredSession();
  status.busy = busy_;
  status.email = session_ ? session_->email : pending_email_;
  status.device_name = device_name_;
  status.last_result = last_result_;
  status.memory_only = memory_only_;
  status.last_upload = last_upload_;
  status.uploaded_tab_count = uploaded_tab_count_;
  status.upload_failing = upload_failing_;
  return status;
}

void TabSyncService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void TabSyncService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void TabSyncService::SetResult(Result result) {
  last_result_ = result;
}

void TabSyncService::NotifyChanged() {
  for (Observer& observer : observers_) {
    observer.OnTabSyncStatusChanged();
  }
}

}  // namespace avora::sync
