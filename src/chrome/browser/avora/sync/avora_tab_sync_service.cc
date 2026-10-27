// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_tab_sync_service.h"

#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/avora/sync/avora_device_name.h"
#include "chrome/browser/avora/sync/avora_sync_config.h"
#include "chrome/browser/profiles/profile.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
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

TabSyncService::TabCollector& GetTabCollector() {
  static base::NoDestructor<TabSyncService::TabCollector> collector;
  return *collector;
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
void TabSyncService::SetTabCollector(TabCollector collector) {
  GetTabCollector() = std::move(collector);
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
          kSupabasePublishableKey)) {
  const auto& delays = DelaysForTesting();
  scheduler_ = std::make_unique<UploadScheduler>(
      base::BindRepeating(&TabSyncService::BuildUploadBody,
                          base::Unretained(this)),
      base::BindRepeating(&TabSyncService::Upload, base::Unretained(this)),
      delays ? delays->first : UploadScheduler::kDefaultQuietPeriod,
      delays ? delays->second : UploadScheduler::kDefaultMaxDelay);

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
      // The startup upload. Debounced rather than immediate: session restore
      // may still be adding tabs, and a half-restored list would briefly
      // replace the full one on the phone.
      scheduler_->MarkDirty();
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
  if (state_ == State::kSignedIn) {
    scheduler_->MarkDirty();
  }
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
  scheduler_->Reset();
  scheduler_->UploadNow();
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
  scheduler_->Reset();
  session_.reset();
  store_->Clear();
  state_ = State::kSignedOut;
  SetResult(Result::kSessionExpired);
  NotifyChanged();
}

// ── Upload ──────────────────────────────────────────────────────────────────

void TabSyncService::OnTabsChanged() {
  if (state_ == State::kSignedIn) {
    scheduler_->MarkDirty();
  }
}

std::string TabSyncService::BuildUploadBody() {
  if (!session_ || device_name_.empty()) {
    return std::string();
  }
  std::vector<OpenTab> tabs;
  if (const TabCollector& collector = GetTabCollector()) {
    std::optional<std::vector<OpenTab>> collected = collector.Run(profile_);
    if (!collected) {
      return std::string();
    }
    tabs = std::move(*collected);
  }
  base::ListValue list = BuildTabsList(tabs);
  last_built_tab_count_ = list.size();
  return BuildDeviceUpsertBody(store_->GetOrCreateDeviceId(), device_name_,
                               CurrentPlatform(), std::move(list));
}

void TabSyncService::Upload(std::string body,
                            base::OnceCallback<void(bool)> done) {
  WithFreshToken(base::BindOnce(&TabSyncService::SendUpload,
                                weak_factory_.GetWeakPtr(), std::move(body),
                                /*retried=*/false, std::move(done)));
}

void TabSyncService::SendUpload(std::string body,
                                bool retried,
                                base::OnceCallback<void(bool)> done,
                                std::optional<std::string> access_token) {
  if (signing_out_) {
    std::move(done).Run(false);
    return;
  }
  if (!access_token) {
    upload_failing_ = session_.has_value();
    std::move(done).Run(false);
    NotifyChanged();
    return;
  }
  upsert_in_flight_ = true;
  const size_t tab_count = last_built_tab_count_;
  std::string body_copy = body;
  client_->UpsertDevice(
      *access_token, std::move(body_copy),
      base::BindOnce(&TabSyncService::OnUploaded, weak_factory_.GetWeakPtr(),
                     std::move(body), retried, tab_count, std::move(done)));
}

void TabSyncService::OnUploaded(std::string body,
                                bool retried,
                                size_t tab_count,
                                base::OnceCallback<void(bool)> done,
                                RequestStatus status) {
  upsert_in_flight_ = false;
  if (signing_out_) {
    std::move(done).Run(false);
    ContinueSignOut();
    return;
  }
  if (status == RequestStatus::kOk) {
    upload_failing_ = false;
    last_upload_ = base::Time::Now();
    uploaded_tab_count_ = tab_count;
    std::move(done).Run(true);
    NotifyChanged();
    return;
  }
  if (status == RequestStatus::kUnauthorized && !retried && session_) {
    // Revoked or expired early: refresh once and try again.
    session_->expires_at = base::Time();
    WithFreshToken(base::BindOnce(&TabSyncService::SendUpload,
                                  weak_factory_.GetWeakPtr(), std::move(body),
                                  /*retried=*/true, std::move(done)));
    return;
  }
  if (status == RequestStatus::kForbidden) {
    // Row-level security refused the upsert: a row with this id exists and
    // belongs to another account (its sign-out could not delete it). Take a
    // new id; the retry builds its body with it.
    store_->RotateDeviceId();
  }
  upload_failing_ = true;
  std::move(done).Run(false);
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
  // Nothing new uploads from here on.
  scheduler_->Reset();
  NotifyChanged();
  if (!upsert_in_flight_) {
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
  scheduler_->Reset();
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
