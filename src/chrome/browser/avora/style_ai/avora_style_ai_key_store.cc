// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/style_ai/avora_style_ai_key_store.h"

#include <utility>

#include "base/base64.h"
#include "base/json/values_util.h"
#include "base/no_destructor.h"
#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace avora {

namespace {

constexpr char kCiphertextKey[] = "ciphertext";
constexpr char kSavedAtKey[] = "saved_at";
constexpr char kInsecureTestKey[] = "insecure_test";

constexpr size_t kMinKeyLength = 20;
constexpr size_t kMaxKeyLength = 256;

}  // namespace

StyleAiKeyStore::StyleAiKeyStore(PrefService* pref_service,
                                 Environment environment)
    : pref_service_(pref_service), environment_(environment) {}

StyleAiKeyStore::~StyleAiKeyStore() = default;

// static
void StyleAiKeyStore::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  // Registered without sync flags: neither the ciphertext nor the model choice
  // leaves this device.
  registry->RegisterDictionaryPref(kCredentialPref);
  registry->RegisterStringPref(kModelPref, kDefaultModel);
}

// static
bool StyleAiKeyStore::IsWellFormedKey(std::string_view key) {
  if (key.size() < kMinKeyLength || key.size() > kMaxKeyLength) {
    return false;
  }
  for (char c : key) {
    // Printable ASCII, excluding the space.
    if (c <= 0x20 || c >= 0x7f) {
      return false;
    }
  }
  return true;
}

// static
const std::vector<std::string>& StyleAiKeyStore::AllowedModels() {
  static const base::NoDestructor<std::vector<std::string>> models(
      std::vector<std::string>{kDefaultModel, "claude-sonnet-5-5",
                               "claude-haiku-5-5"});
  return *models;
}

void StyleAiKeyStore::SetEncryptor(
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  encryptor_ = std::move(encryptor);
}

StyleAiKeyStore::SaveResult StyleAiKeyStore::SaveKey(std::string_view key) {
  if (!IsWellFormedKey(key)) {
    return SaveResult::kInvalidFormat;
  }
  if (environment_ == Environment::kMockKeychain) {
    return SaveResult::kInsecureStorageRefused;
  }
  if (!is_ready()) {
    return SaveResult::kStorageNotReady;
  }
  if (!encryptor_->IsEncryptionAvailable()) {
    return SaveResult::kStorageUnavailable;
  }
  std::string ciphertext;
  if (!encryptor_->EncryptString(std::string(key), &ciphertext) ||
      ciphertext.empty()) {
    // Storing the key unencrypted would be worse than not storing it.
    return SaveResult::kStorageUnavailable;
  }
  base::DictValue entry;
  entry.Set(kCiphertextKey, base::Base64Encode(ciphertext));
  entry.Set(kSavedAtKey,
            base::Int64ToValue(
                base::Time::Now().InMillisecondsSinceUnixEpoch()));
  if (environment_ == Environment::kInsecureTest) {
    entry.Set(kInsecureTestKey, true);
  }
  // One write replaces the whole entry, so a replacement is atomic.
  pref_service_->SetDict(kCredentialPref, std::move(entry));
  return SaveResult::kSaved;
}

void StyleAiKeyStore::RemoveKey() {
  pref_service_->ClearPref(kCredentialPref);
}

StyleAiKeyStore::KeyState StyleAiKeyStore::GetKeyState() const {
  const base::DictValue& entry = pref_service_->GetDict(kCredentialPref);
  const std::string* encoded = entry.FindString(kCiphertextKey);
  if (!encoded || encoded->empty()) {
    return KeyState::kAbsent;
  }
  if (!is_ready()) {
    return KeyState::kStorageNotReady;
  }
  return GetKeyForRequest() ? KeyState::kPresent : KeyState::kUndecryptable;
}

std::optional<base::Time> StyleAiKeyStore::GetSavedTime() const {
  const base::DictValue& entry = pref_service_->GetDict(kCredentialPref);
  if (!entry.FindString(kCiphertextKey)) {
    return std::nullopt;
  }
  const base::Value* saved_at = entry.Find(kSavedAtKey);
  std::optional<int64_t> millis =
      saved_at ? base::ValueToInt64(*saved_at) : std::nullopt;
  if (!millis) {
    return std::nullopt;
  }
  return base::Time::FromMillisecondsSinceUnixEpoch(*millis);
}

bool StyleAiKeyStore::IsInsecureTestKey() const {
  return pref_service_->GetDict(kCredentialPref)
      .FindBool(kInsecureTestKey)
      .value_or(false);
}

std::optional<std::string> StyleAiKeyStore::GetKeyForRequest() const {
  if (!is_ready()) {
    return std::nullopt;
  }
  // A key saved under the insecure test environment is never usable outside
  // it, even if it happens to decrypt.
  if (IsInsecureTestKey() && environment_ != Environment::kInsecureTest) {
    return std::nullopt;
  }
  const std::string* encoded =
      pref_service_->GetDict(kCredentialPref).FindString(kCiphertextKey);
  if (!encoded || encoded->empty()) {
    return std::nullopt;
  }
  std::string ciphertext;
  if (!base::Base64Decode(*encoded, &ciphertext)) {
    return std::nullopt;
  }
  std::string plaintext;
  if (!encryptor_->DecryptString(ciphertext, &plaintext) ||
      !IsWellFormedKey(plaintext)) {
    return std::nullopt;
  }
  return plaintext;
}

std::string StyleAiKeyStore::GetModel() const {
  const std::string& model = pref_service_->GetString(kModelPref);
  for (const std::string& allowed : AllowedModels()) {
    if (model == allowed) {
      return model;
    }
  }
  return kDefaultModel;
}

bool StyleAiKeyStore::SetModel(std::string_view model) {
  for (const std::string& allowed : AllowedModels()) {
    if (model == allowed) {
      pref_service_->SetString(kModelPref, allowed);
      return true;
    }
  }
  return false;
}

}  // namespace avora
