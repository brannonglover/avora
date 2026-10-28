// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_SYNC_AVORA_TAB_SYNC_SERVICE_H_
#define CHROME_BROWSER_AVORA_SYNC_AVORA_TAB_SYNC_SERVICE_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/supports_user_data.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/avora/sync/avora_shared_models.h"
#include "chrome/browser/avora/sync/avora_shared_store.h"
#include "chrome/browser/avora/sync/avora_supabase_client.h"
#include "chrome/browser/avora/sync/avora_sync_session_store.h"
#include "url/gurl.h"

class PrefRegistrySimple;
class Profile;

namespace os_crypt_async {
class Encryptor;
class OSCryptAsync;
}  // namespace os_crypt_async

namespace avora::sync {

// Avora account sign-in and shared tabs for one regular profile.
//
// Signing in is two steps: an email address, to which Supabase sends a
// one-time code, then that code, which buys a session. While signed in, the
// profile's Spaces, Favorites, pinned items and today tabs are one shared set
// with every other device on the account (docs/SYNC.md in the avoraapp
// repository): this device's changes go up as row writes a moment after they
// happen, and other devices' changes come down every few seconds and are
// applied to the sidebar models and the tab strips.
//
// Spaces, Favorites and pinned items are diffed from their models
// (SharedModels). Today tabs live in the tab strips, which this target can't
// see: the views layer reports each one opened, navigated or closed by the
// user, and opens, updates and closes them for remote changes (TodayTabs).
//
// Signing out deletes this device's public.devices row and forgets the
// session and the shared rows; the tabs stay open here.
//
// Attached to the Profile as user data, like LiveFolderService. Off-the-record
// profiles have none: their tabs are never shared.
class TabSyncService : public base::SupportsUserData::Data {
 public:
  enum class State {
    kSignedOut,
    // A code was emailed to pending_email; waiting for the user to enter it.
    kAwaitingCode,
    kSignedIn,
  };

  // What the last user action, or the last thing that happened to the
  // session, came to. For the settings page's message line.
  enum class Result {
    kNone,
    kCodeSent,
    kSignedIn,
    kSignedOut,
    kInvalidEmail,
    kInvalidCode,
    kWrongCode,
    kRateLimited,
    kNetworkError,
    // The refresh token was refused, so the user has to sign in again.
    kSessionExpired,
    // Signed out, but the device row could not be deleted (offline), so the
    // phone may keep showing these tabs. This install has a new id now.
    kSignedOutDeviceNotRemoved,
  };

  struct Status {
    State state = State::kSignedOut;
    // A stored session is waiting for the encryptor before it can be read.
    bool starting = false;
    // A sign-in or sign-out request is in flight.
    bool busy = false;
    // The signed-in email, or the one the code went to.
    std::string email;
    std::string device_name;
    Result last_result = Result::kNone;
    // The session lives in memory only and ends when Avora quits.
    bool memory_only = false;
    // When this device last finished a sync with the server, if ever this
    // run.
    base::Time last_upload;
    // How many tabs (today tabs, pinned items and Favorites) the account has.
    size_t uploaded_tab_count = 0;
    // The most recent sync attempt failed; it is being retried.
    bool upload_failing = false;
  };

  class Observer : public base::CheckedObserver {
   public:
    virtual void OnTabSyncStatusChanged() = 0;
  };

  // One today tab, in local ids: `guid` is its Avora tab GUID and
  // `space_id` its SpaceManager id.
  struct TodayTab {
    std::string guid;
    std::string space_id;
    GURL url;
    std::u16string title;
  };

  // The tab strips, from //chrome/browser/ui/views/avora, which this target
  // can't depend on.
  class TodayTabs {
   public:
    virtual ~TodayTabs() = default;
    // Whether the tab strips can be changed: false while session restore is
    // still adding tabs, or before any window exists.
    virtual bool IsReady(Profile* profile) = 0;
    // Every today tab of the profile, once per GUID, in tab strip order
    // within each Space.
    virtual std::vector<TodayTab> Collect(Profile* profile) = 0;
    // Opens `tab` (unloaded) in each window that shows its Space, after the
    // last of `before` (the GUIDs ahead of it, in order) that the window has,
    // or updates the copies that are already open.
    virtual void Upsert(Profile* profile,
                        const TodayTab& tab,
                        const std::vector<std::string>& before) = 0;
    // Closes every copy of the tab.
    virtual void Remove(Profile* profile, const std::string& guid) = 0;
    // Removes a pinned item and unmarks its open tab.
    virtual void RemovePinnedItem(Profile* profile,
                                  const std::string& item_id) = 0;
  };

  // Set once from //chrome/browser (browser_view.cc), as for StyleAiSettings.
  static void SetOSCryptAsync(os_crypt_async::OSCryptAsync* os_crypt_async);
  static void SetTodayTabs(TodayTabs* today_tabs);

  // Null for a null or off-the-record profile.
  static TabSyncService* GetForProfile(Profile* profile);
  // As above, but never creates one.
  static TabSyncService* GetForProfileIfExists(Profile* profile);

  // For services created after this call: how long local changes wait
  // before they are sent, and how often other devices' changes are fetched.
  // Tests use short ones.
  static void SetUploadDelaysForTesting(base::TimeDelta quiet_period,
                                        base::TimeDelta max_delay);

  explicit TabSyncService(Profile* profile);
  TabSyncService(const TabSyncService&) = delete;
  TabSyncService& operator=(const TabSyncService&) = delete;
  ~TabSyncService() override;

  // Emails a one-time code to `email`. From kSignedOut or kAwaitingCode (to
  // resend, or to switch address).
  void RequestCode(const std::string& email);

  // Signs in with the code sent to the pending email.
  void VerifyCode(const std::string& code);

  // Abandons a pending code and goes back to asking for an email.
  void CancelSignIn();

  // Deletes this device's row, revokes the session and forgets it.
  void SignOut();

  // The user opened a today tab, or moved one to another Space. `predecessor`
  // and `successor` are the GUIDs of its neighbours in the Space, if any.
  void OnTodayTabOpened(const TodayTab& tab,
                        const std::string& predecessor,
                        const std::string& successor);
  // A loaded today tab navigated or was retitled.
  void OnTodayTabNavigated(const TodayTab& tab);
  // The user closed a today tab, or it became a Favorite or pinned item.
  void OnTodayTabClosed(const std::string& guid);

  // The account's today tabs in a Space, in order, for a window showing the
  // Space for the first time. Empty while signed out.
  std::vector<TodayTab> TodayTabsForSpace(const std::string& space_id) const;

  // Fetches other devices' changes now, e.g. when a window comes to the
  // front.
  void SyncSoon();

  // ── Tab previews (docs/SYNC.md) ──
  static constexpr char kPreviewsEnabledPref[] = "avora.sync.previews_enabled";
  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  bool previews_enabled() const;
  // Turning previews off deletes the ones this account has.
  void SetPreviewsEnabled(bool enabled);
  // Whether a fresh preview of the today tab `guid` would be used now:
  // signed in, previews on, the tab shared, and none sent in the last minute.
  bool WantsPreview(const std::string& guid) const;
  // A JPEG of the tab's visible page, already checked to be shareable.
  void OnPreviewCaptured(const std::string& guid, std::string jpeg);
  // The tab's page may not have a preview (a password field, no-store): any
  // it has is deleted.
  void OnPreviewNotAllowed(const std::string& guid);

  Status GetStatus() const;
  State state() const { return state_; }

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  const SharedStore& shared_store_for_testing() const { return *shared_; }

 private:
  using TokenCallback =
      base::OnceCallback<void(std::optional<std::string> access_token)>;

  void OnEncryptorReady(scoped_refptr<os_crypt_async::Encryptor> encryptor);
  void OnDeviceName(std::string name);

  void OnCodeRequested(std::string email, RequestStatus status);
  void OnCodeVerified(RequestStatus status, std::optional<AuthSession> session);
  void StartSession(AuthSession session);

  // Runs `callback` with an access token that is good for at least a minute,
  // refreshing first if needed; with null if there is no session or it could
  // not be refreshed.
  void WithFreshToken(TokenCallback callback);
  void OnRefreshed(RequestStatus status, std::optional<AuthSession> session);

  // ── Shared rows ──
  bool IsSyncing() const;
  void StartSync(bool fresh_sign_in);
  void StopSync();
  void UpsertDeviceRow();
  void Pull();
  void FetchTable(SupabaseClient::Table table,
                  std::optional<std::string> since,
                  size_t offset,
                  base::ListValue rows,
                  base::OnceCallback<void(bool, base::ListValue)> done,
                  std::optional<std::string> access_token);
  void OnTableFetched(SupabaseClient::Table table,
                      std::optional<std::string> since,
                      size_t offset,
                      base::ListValue rows,
                      base::OnceCallback<void(bool, base::ListValue)> done,
                      RequestStatus status,
                      std::optional<base::ListValue> page);
  void OnSpacesPulled(bool full, bool ok, base::ListValue spaces);
  void OnPulled(bool full,
                base::ListValue spaces,
                bool ok,
                base::ListValue items);
  void FinishFirstLoad();
  void ReconcileModels(bool diff_first);
  void RebaseModels();
  void OnModelsChanged();
  void ApplyTodayRows(const std::vector<std::string>& shared_ids);
  void RetryDeferredToday();
  std::string PositionBetween(const std::string& shared_space,
                              const std::string& predecessor_guid,
                              const std::string& successor_guid) const;
  void SchedulePush(base::TimeDelta delay);
  void Push();
  void SendPush(bool retried, std::optional<std::string> access_token);
  void PushTable(SupabaseClient::Table table,
                 bool retried,
                 std::string access_token);
  void OnPushed(SupabaseClient::Table table,
                bool retried,
                std::string access_token,
                RequestStatus status);
  void FinishPushing(bool ok);
  void OnSyncFailed();
  void OnSyncSucceeded();
  void DeletePreviewOf(const std::string& shared_id);

  // Deletes the device row once no push is in flight.
  void ContinueSignOut();
  void DeleteDeviceWithToken(std::optional<std::string> access_token);
  void OnDeviceDeleted(std::string access_token, RequestStatus status);
  void FinishSignOut(Result result);

  // The session was refused for good: forget it.
  void ExpireSession();

  void SetResult(Result result);
  void NotifyChanged();

  raw_ptr<Profile> profile_;
  std::unique_ptr<SessionStore> store_;
  std::unique_ptr<SupabaseClient> client_;
  std::unique_ptr<SharedStore> shared_;
  std::unique_ptr<SharedModels> models_;

  State state_ = State::kSignedOut;
  bool busy_ = false;
  std::optional<AuthSession> session_;
  bool memory_only_ = false;
  std::string pending_email_;
  std::string device_name_;
  Result last_result_ = Result::kNone;

  bool signing_out_ = false;
  bool refresh_in_flight_ = false;
  std::vector<TokenCallback> token_waiters_;

  const base::TimeDelta push_delay_;
  const base::TimeDelta pull_interval_;
  bool pulling_ = false;
  bool pushing_ = false;
  bool push_again_ = false;
  // Applying server rows: model changes are not this device's own.
  bool applying_ = false;
  bool device_row_written_ = false;
  int failures_ = 0;
  // When each tab's preview was last sent, by GUID.
  std::map<std::string, base::TimeTicks> last_preview_;
  // Today rows that arrived while the tab strips weren't ready.
  std::vector<std::string> deferred_today_;
  base::RepeatingTimer pull_timer_;
  base::OneShotTimer push_timer_;
  base::OneShotTimer reconcile_timer_;
  base::OneShotTimer retry_timer_;

  base::Time last_upload_;
  size_t uploaded_tab_count_ = 0;
  bool upload_failing_ = false;

  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<TabSyncService> weak_factory_{this};
};

}  // namespace avora::sync

#endif  // CHROME_BROWSER_AVORA_SYNC_AVORA_TAB_SYNC_SERVICE_H_
