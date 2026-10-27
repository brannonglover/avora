// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_sync_session_store.h"

#include <utility>

#include "base/base64.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/uuid.h"
#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace avora::sync {

namespace {

constexpr char kCiphertextKey[] = "ciphertext";

constexpr char kAccessTokenKey[] = "access_token";
constexpr char kRefreshTokenKey[] = "refresh_token";
constexpr char kExpiresAtKey[] = "expires_at";
constexpr char kUserIdKey[] = "user_id";
constexpr char kEmailKey[] = "email";

std::optional<std::string> Serialize(const AuthSession& session) {
  base::DictValue dict;
  dict.Set(kAccessTokenKey, session.access_token);
  dict.Set(kRefreshTokenKey, session.refresh_token);
  dict.Set(kExpiresAtKey, session.expires_at.InMillisecondsFSinceUnixEpoch());
  dict.Set(kUserIdKey, session.user_id);
  dict.Set(kEmailKey, session.email);
  return base::WriteJson(dict);
}

std::optional<AuthSession> Deserialize(std::string_view json) {
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!dict) {
    return std::nullopt;
  }
  const std::string* access_token = dict->FindString(kAccessTokenKey);
  const std::string* refresh_token = dict->FindString(kRefreshTokenKey);
  std::optional<double> expires_at = dict->FindDouble(kExpiresAtKey);
  if (!access_token || !refresh_token || refresh_token->empty() ||
      !expires_at) {
    return std::nullopt;
  }
  AuthSession session;
  session.access_token = *access_token;
  session.refresh_token = *refresh_token;
  session.expires_at = base::Time::FromMillisecondsSinceUnixEpoch(*expires_at);
  if (const std::string* user_id = dict->FindString(kUserIdKey)) {
    session.user_id = *user_id;
  }
  if (const std::string* email = dict->FindString(kEmailKey)) {
    session.email = *email;
  }
  return session;
}

}  // namespace

SessionStore::SessionStore(PrefService* pref_service, Environment environment)
    : pref_service_(pref_service), environment_(environment) {}

SessionStore::~SessionStore() = default;

// static
void SessionStore::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  // Registered without sync flags: none of this leaves the device through
  // Chrome Sync.
  registry->RegisterDictionaryPref(kSessionPref);
  registry->RegisterStringPref(kEmailPref, std::string());
  registry->RegisterStringPref(kDeviceIdPref, std::string());
}

void SessionStore::SetEncryptor(
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  encryptor_ = std::move(encryptor);
}

bool SessionStore::HasStoredSession() const {
  if (memory_session_) {
    return true;
  }
  const std::string* encoded =
      pref_service_->GetDict(kSessionPref).FindString(kCiphertextKey);
  return encoded && !encoded->empty();
}

std::optional<AuthSession> SessionStore::Load() const {
  if (memory_session_) {
    return memory_session_;
  }
  if (!is_ready()) {
    return std::nullopt;
  }
  const std::string* encoded =
      pref_service_->GetDict(kSessionPref).FindString(kCiphertextKey);
  if (!encoded || encoded->empty()) {
    return std::nullopt;
  }
  std::string ciphertext;
  if (!base::Base64Decode(*encoded, &ciphertext)) {
    return std::nullopt;
  }
  std::string plaintext;
  if (!encryptor_->DecryptString(ciphertext, &plaintext)) {
    return std::nullopt;
  }
  return Deserialize(plaintext);
}

SessionStore::SaveResult SessionStore::Save(const AuthSession& session) {
  pref_service_->SetString(kEmailPref, session.email);

  std::optional<std::string> plaintext = Serialize(session);
  std::string ciphertext;
  const bool can_persist = environment_ == Environment::kSecure &&
                           is_ready() && encryptor_->IsEncryptionAvailable();
  if (can_persist && plaintext &&
      encryptor_->EncryptString(*plaintext, &ciphertext) &&
      !ciphertext.empty()) {
    base::DictValue entry;
    entry.Set(kCiphertextKey, base::Base64Encode(ciphertext));
    // One write replaces the whole entry, so a token rotation is atomic.
    pref_service_->SetDict(kSessionPref, std::move(entry));
    memory_session_.reset();
    return SaveResult::kPersisted;
  }

  // Storing tokens unencrypted would be worse than signing in again after a
  // restart. Drop any older ciphertext so it cannot outlive this session.
  pref_service_->ClearPref(kSessionPref);
  memory_session_ = session;
  return SaveResult::kMemoryOnly;
}

void SessionStore::Clear() {
  memory_session_.reset();
  pref_service_->ClearPref(kSessionPref);
  pref_service_->ClearPref(kEmailPref);
}

std::string SessionStore::GetEmail() const {
  if (memory_session_) {
    return memory_session_->email;
  }
  return pref_service_->GetString(kEmailPref);
}

std::string SessionStore::GetOrCreateDeviceId() {
  const std::string& stored = pref_service_->GetString(kDeviceIdPref);
  if (base::Uuid::ParseLowercase(stored).is_valid()) {
    return stored;
  }
  RotateDeviceId();
  return pref_service_->GetString(kDeviceIdPref);
}

void SessionStore::RotateDeviceId() {
  pref_service_->SetString(kDeviceIdPref,
                           base::Uuid::GenerateRandomV4().AsLowercaseString());
}

}  // namespace avora::sync
