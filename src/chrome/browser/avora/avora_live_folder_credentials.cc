// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_live_folder_credentials.h"

#include <utility>

#include "base/base64.h"
#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace avora {

namespace {

constexpr char kTokenKey[] = "token";
constexpr char kAccountKey[] = "account";

}  // namespace

LiveFolderCredentialStore::LiveFolderCredentialStore(PrefService* pref_service)
    : pref_service_(pref_service) {
  pref_available_ =
      pref_service_ && pref_service_->FindPreference(kCredentialsPref);
}

LiveFolderCredentialStore::~LiveFolderCredentialStore() = default;

// static
void LiveFolderCredentialStore::RegisterProfilePrefs(
    PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kCredentialsPref);
}

void LiveFolderCredentialStore::SetEncryptor(
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  encryptor_ = std::move(encryptor);
}

const base::DictValue* LiveFolderCredentialStore::FindEntry(
    const std::string& space_id,
    LiveFolderProviderId provider) const {
  if (!pref_available_ || space_id.empty() || !is_ready()) {
    return nullptr;
  }
  const base::DictValue& by_space = pref_service_->GetDict(kCredentialsPref);
  const base::DictValue* by_provider = by_space.FindDict(space_id);
  if (!by_provider) {
    return nullptr;
  }
  return by_provider->FindDict(LiveFolderProviderIdToString(provider));
}

std::string LiveFolderCredentialStore::GetToken(
    const std::string& space_id,
    LiveFolderProviderId provider) const {
  const base::DictValue* entry = FindEntry(space_id, provider);
  if (!entry) {
    return std::string();
  }
  const std::string* encoded = entry->FindString(kTokenKey);
  if (!encoded || encoded->empty()) {
    return std::string();
  }

  // Base64 because the encryptor emits raw bytes and prefs serialize as JSON,
  // which cannot carry them.
  std::string ciphertext;
  if (!base::Base64Decode(*encoded, &ciphertext)) {
    return std::string();
  }
  std::string plaintext;
  if (!encryptor_->DecryptString(ciphertext, &plaintext)) {
    return std::string();
  }
  return plaintext;
}

std::string LiveFolderCredentialStore::GetAccount(
    const std::string& space_id,
    LiveFolderProviderId provider) const {
  const base::DictValue* entry = FindEntry(space_id, provider);
  if (!entry) {
    return std::string();
  }
  const std::string* account = entry->FindString(kAccountKey);
  return account ? *account : std::string();
}

bool LiveFolderCredentialStore::HasToken(const std::string& space_id,
                                         LiveFolderProviderId provider) const {
  return !GetToken(space_id, provider).empty();
}

void LiveFolderCredentialStore::SetToken(const std::string& space_id,
                                         LiveFolderProviderId provider,
                                         const std::string& token,
                                         const std::string& account) {
  if (!pref_available_ || space_id.empty() || !is_ready()) {
    return;
  }

  std::string ciphertext;
  if (!encryptor_->EncryptString(token, &ciphertext)) {
    // Storing the token unencrypted would be worse than not storing it: the
    // user can reconnect, but a cleartext token on disk is permanent.
    return;
  }
  const std::string encrypted = base::Base64Encode(ciphertext);

  ScopedDictPrefUpdate update(pref_service_, kCredentialsPref);
  base::DictValue* by_provider = update->EnsureDict(space_id);
  base::DictValue entry;
  entry.Set(kTokenKey, encrypted);
  entry.Set(kAccountKey, account);
  by_provider->Set(LiveFolderProviderIdToString(provider), std::move(entry));
}

void LiveFolderCredentialStore::ClearToken(const std::string& space_id,
                                           LiveFolderProviderId provider) {
  if (!pref_available_ || space_id.empty()) {
    return;
  }
  ScopedDictPrefUpdate update(pref_service_, kCredentialsPref);
  base::DictValue* by_provider = update->FindDict(space_id);
  if (!by_provider) {
    return;
  }
  by_provider->Remove(LiveFolderProviderIdToString(provider));
  if (by_provider->empty()) {
    update->Remove(space_id);
  }
}

void LiveFolderCredentialStore::ClearSpace(const std::string& space_id) {
  if (!pref_available_ || space_id.empty()) {
    return;
  }
  ScopedDictPrefUpdate update(pref_service_, kCredentialsPref);
  update->Remove(space_id);
}

}  // namespace avora
