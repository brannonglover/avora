// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_SYNC_AVORA_TAB_SYNC_SERVICE_H_
#define CHROME_BROWSER_AVORA_SYNC_AVORA_TAB_SYNC_SERVICE_H_

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
#include "chrome/browser/avora/sync/avora_supabase_client.h"
#include "chrome/browser/avora/sync/avora_sync_session_store.h"
#include "chrome/browser/avora/sync/avora_tab_snapshot.h"
#include "chrome/browser/avora/sync/avora_upload_scheduler.h"

class Profile;

namespace os_crypt_async {
class Encryptor;
class OSCryptAsync;
}  // namespace os_crypt_async

namespace avora::sync {

// Avora account sign-in and tab upload for one regular profile.
//
// Signing in is two steps: an email address, to which Supabase sends a
// one-time code, then that code, which buys a session. While signed in, every
// change to the profile's tabs (reported by the tab watcher in
// //chrome/browser/ui/views/avora) marks the snapshot dirty, and
// UploadScheduler upserts this device's public.devices row with the full set a
// few seconds later. Signing out deletes the row, then forgets the session.
// The contract with the phone app is docs/SYNC.md in the avoraapp repository.
//
// Attached to the Profile as user data, like LiveFolderService. Off-the-record
// profiles have none: their tabs are never uploaded.
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
    // When the server last accepted this device's tabs, if ever this run.
    base::Time last_upload;
    // How many tabs that upload carried.
    size_t uploaded_tab_count = 0;
    // The most recent upload attempt failed; it is being retried.
    bool upload_failing = false;
  };

  class Observer : public base::CheckedObserver {
   public:
    virtual void OnTabSyncStatusChanged() = 0;
  };

  // Lists every open tab of a profile, across all of its windows and Spaces,
  // or null while the browser is quitting: windows closing on the way out are
  // not tabs being closed, and uploading what is left would empty the phone's
  // list until the next launch. Supplied from //chrome/browser/ui/views/avora,
  // which this target cannot depend on.
  using TabCollector =
      base::RepeatingCallback<std::optional<std::vector<OpenTab>>(
          Profile* profile)>;

  // Set once from //chrome/browser (browser_view.cc), as for StyleAiSettings.
  static void SetOSCryptAsync(os_crypt_async::OSCryptAsync* os_crypt_async);
  static void SetTabCollector(TabCollector collector);

  // Null for a null or off-the-record profile.
  static TabSyncService* GetForProfile(Profile* profile);
  // As above, but never creates one.
  static TabSyncService* GetForProfileIfExists(Profile* profile);

  // Upload debounce for services created after this call; tests use short
  // ones.
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

  // The profile's tabs changed in a way the phone would see.
  void OnTabsChanged();

  Status GetStatus() const;
  State state() const { return state_; }

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // The body the next upload would send, built now. Empty when signed out,
  // while quitting, or before the device name is known.
  std::string BuildUploadBodyForTesting() { return BuildUploadBody(); }

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

  std::string BuildUploadBody();
  void Upload(std::string body, base::OnceCallback<void(bool)> done);
  void SendUpload(std::string body,
                  bool retried,
                  base::OnceCallback<void(bool)> done,
                  std::optional<std::string> access_token);
  void OnUploaded(std::string body,
                  bool retried,
                  size_t tab_count,
                  base::OnceCallback<void(bool)> done,
                  RequestStatus status);

  // Deletes the row once no upsert is in flight: one that already left could
  // otherwise reach the server after the delete and put the row back.
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
  std::unique_ptr<UploadScheduler> scheduler_;

  State state_ = State::kSignedOut;
  bool busy_ = false;
  std::optional<AuthSession> session_;
  bool memory_only_ = false;
  std::string pending_email_;
  std::string device_name_;
  Result last_result_ = Result::kNone;

  bool signing_out_ = false;
  bool upsert_in_flight_ = false;
  bool refresh_in_flight_ = false;
  std::vector<TokenCallback> token_waiters_;

  // Tab count of the body most recently built, reported once it is accepted.
  size_t last_built_tab_count_ = 0;
  base::Time last_upload_;
  size_t uploaded_tab_count_ = 0;
  bool upload_failing_ = false;

  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<TabSyncService> weak_factory_{this};
};

}  // namespace avora::sync

#endif  // CHROME_BROWSER_AVORA_SYNC_AVORA_TAB_SYNC_SERVICE_H_
