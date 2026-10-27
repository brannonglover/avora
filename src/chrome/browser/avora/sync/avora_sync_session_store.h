// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_SYNC_AVORA_SYNC_SESSION_STORE_H_
#define CHROME_BROWSER_AVORA_SYNC_AVORA_SYNC_SESSION_STORE_H_

#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "chrome/browser/avora/sync/avora_supabase_client.h"
#include "components/os_crypt/async/common/encryptor.h"

class PrefRegistrySimple;
class PrefService;

namespace avora::sync {

// Where tab sync keeps its account session and this device's id.
//
// The session (access token, refresh token, expiry, user) is encrypted with
// OSCryptAsync before it is written -- on macOS with the key in the "Avora
// Safe Storage" Keychain item, as for Style Studio AI's key and Live Folders
// tokens -- and the pref holds only Base64 ciphertext. Under
// --use-mock-keychain, or when OS secure storage is unavailable, a session is
// kept in memory only: signed in until quit, never written weakly protected.
//
// The device id is a UUID generated the first time it is needed and kept for
// good, including across sign-out, so this install is always the same row.
class SessionStore {
 public:
  // Dict{ciphertext: Base64}. Not syncable.
  static constexpr char kSessionPref[] = "avora.sync.session";
  // The signed-in email, for display before the encryptor is ready.
  static constexpr char kEmailPref[] = "avora.sync.email";
  static constexpr char kDeviceIdPref[] = "avora.sync.device_id";

  enum class Environment {
    kSecure,
    // --use-mock-keychain: sessions are kept in memory only.
    kMockKeychain,
  };

  enum class SaveResult {
    kPersisted,
    // Encrypted storage was unavailable; the session lasts until quit.
    kMemoryOnly,
  };

  SessionStore(PrefService* pref_service, Environment environment);
  SessionStore(const SessionStore&) = delete;
  SessionStore& operator=(const SessionStore&) = delete;
  ~SessionStore();

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  // Supplies the encryptor once it is available. Before that, a stored
  // session cannot be read.
  void SetEncryptor(scoped_refptr<os_crypt_async::Encryptor> encryptor);
  bool is_ready() const { return encryptor_ != nullptr; }

  // Whether ciphertext is stored, whether or not it can be read here.
  bool HasStoredSession() const;

  // The session, from memory or decrypted from prefs. Null when signed out,
  // when the encryptor is not ready, or when the stored session cannot be
  // decrypted (profile copied from another Mac, Keychain item reset).
  std::optional<AuthSession> Load() const;

  // Replaces the session, for example after a refresh rotated its tokens.
  SaveResult Save(const AuthSession& session);

  // Forgets the session everywhere. The device id is kept.
  void Clear();

  // The email of the stored session, readable without the encryptor.
  std::string GetEmail() const;

  std::string GetOrCreateDeviceId();

  // Gives this install a new id. Used when the old row could not be removed
  // from the previous account, which would otherwise own it forever.
  void RotateDeviceId();

 private:
  raw_ptr<PrefService> pref_service_;
  const Environment environment_;
  scoped_refptr<os_crypt_async::Encryptor> encryptor_;
  // Set when the session could not be persisted.
  std::optional<AuthSession> memory_session_;
};

}  // namespace avora::sync

#endif  // CHROME_BROWSER_AVORA_SYNC_AVORA_SYNC_SESSION_STORE_H_
