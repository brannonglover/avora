// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/style_ai/avora_style_ai_key_store.h"

#include <memory>
#include <string>

#include "base/command_line.h"
#include "base/values.h"
#include "chrome/browser/avora/style_ai/avora_style_ai_settings.h"
#include "components/os_crypt/async/browser/test_utils.h"
#include "components/os_crypt/common/os_crypt_switches.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace avora {
namespace {

using Environment = StyleAiKeyStore::Environment;
using KeyState = StyleAiKeyStore::KeyState;
using SaveResult = StyleAiKeyStore::SaveResult;

constexpr char kKey[] = "sk-ant-api03-TESTKEYabcdefghijklmnopqrstuvwxyz0123";
constexpr char kOtherKey[] = "sk-ant-api03-OTHERKEYzyxwvutsrqponmlkjihgfedcba98";

class StyleAiKeyStoreTest : public testing::Test {
 protected:
  void SetUp() override {
    StyleAiKeyStore::RegisterProfilePrefs(prefs_.registry());
  }

  std::unique_ptr<StyleAiKeyStore> MakeStore(
      Environment environment = Environment::kSecure,
      bool with_encryptor = true) {
    auto store = std::make_unique<StyleAiKeyStore>(&prefs_, environment);
    if (with_encryptor) {
      store->SetEncryptor(encryptor_);
    }
    return store;
  }

  // Everything the credential pref holds, serialised.
  std::string StoredCredential() const {
    std::string out;
    for (auto [name, value] :
         prefs_.GetDict(StyleAiKeyStore::kCredentialPref)) {
      out += name + "=" + value.DebugString();
    }
    return out;
  }

  TestingPrefServiceSimple prefs_;
  scoped_refptr<os_crypt_async::Encryptor> encryptor_ =
      os_crypt_async::GetTestEncryptorForTesting();
};

TEST_F(StyleAiKeyStoreTest, SavesEncryptedAndReadsBack) {
  auto store = MakeStore();
  EXPECT_EQ(store->GetKeyState(), KeyState::kAbsent);
  EXPECT_EQ(store->SaveKey(kKey), SaveResult::kSaved);
  EXPECT_EQ(store->GetKeyState(), KeyState::kPresent);
  EXPECT_EQ(store->GetKeyForRequest(), std::string(kKey));
  EXPECT_TRUE(store->GetSavedTime().has_value());
  EXPECT_FALSE(store->IsInsecureTestKey());
}

TEST_F(StyleAiKeyStoreTest, PrefHoldsNoPlaintext) {
  auto store = MakeStore();
  ASSERT_EQ(store->SaveKey(kKey), SaveResult::kSaved);
  const std::string stored = StoredCredential();
  EXPECT_FALSE(stored.empty());
  EXPECT_EQ(stored.find(kKey), std::string::npos);
  // Not even a recognisable fragment of the key.
  EXPECT_EQ(stored.find("TESTKEY"), std::string::npos);
}

TEST_F(StyleAiKeyStoreTest, PrefsAreNotSyncable) {
  EXPECT_EQ(prefs_.registry()->GetRegistrationFlags(
                StyleAiKeyStore::kCredentialPref),
            0u);
  EXPECT_EQ(prefs_.registry()->GetRegistrationFlags(StyleAiKeyStore::kModelPref),
            0u);
}

TEST_F(StyleAiKeyStoreTest, RejectsMalformedKeysWithoutStoring) {
  auto store = MakeStore();
  for (const char* bad : {"", "short-key", "sk-ant has a space in the middle",
                          "sk-ant-api03-with-newline\nabcdefghij",
                          "sk-ant-api03-\xc3\xa9-not-ascii-abcdefghij"}) {
    EXPECT_EQ(store->SaveKey(bad), SaveResult::kInvalidFormat) << bad;
  }
  EXPECT_EQ(store->SaveKey(std::string(257, 'a')), SaveResult::kInvalidFormat);
  EXPECT_EQ(store->GetKeyState(), KeyState::kAbsent);
  EXPECT_TRUE(StoredCredential().empty());
}

TEST_F(StyleAiKeyStoreTest, RefusesToSaveBeforeTheEncryptorArrives) {
  auto store = MakeStore(Environment::kSecure, /*with_encryptor=*/false);
  EXPECT_EQ(store->SaveKey(kKey), SaveResult::kStorageNotReady);
  EXPECT_TRUE(StoredCredential().empty());
  EXPECT_FALSE(store->GetKeyForRequest().has_value());
}

TEST_F(StyleAiKeyStoreTest, ReportsNotReadyForAStoredKeyBeforeTheEncryptor) {
  ASSERT_EQ(MakeStore()->SaveKey(kKey), SaveResult::kSaved);
  auto store = MakeStore(Environment::kSecure, /*with_encryptor=*/false);
  EXPECT_EQ(store->GetKeyState(), KeyState::kStorageNotReady);
}

TEST_F(StyleAiKeyStoreTest, RefusesWhenSecureStorageIsUnavailable) {
  auto first = MakeStore();
  ASSERT_EQ(first->SaveKey(kKey), SaveResult::kSaved);
  const std::string before = StoredCredential();

  // As when Keychain access is denied: no key to encrypt with.
  auto store = std::make_unique<StyleAiKeyStore>(&prefs_, Environment::kSecure);
  store->SetEncryptor(os_crypt_async::GetTestEncryptorWithoutKeysForTesting());
  EXPECT_EQ(store->SaveKey(kOtherKey), SaveResult::kStorageUnavailable);
  // The existing key is left exactly as it was.
  EXPECT_EQ(StoredCredential(), before);
}

TEST_F(StyleAiKeyStoreTest, RefusesWhenEncryptionIsReportedUnavailable) {
  auto encryptor = os_crypt_async::GetTestEncryptorForTesting();
  encryptor->set_encryption_available_for_testing(false);
  auto store = std::make_unique<StyleAiKeyStore>(&prefs_, Environment::kSecure);
  store->SetEncryptor(encryptor);
  EXPECT_EQ(store->SaveKey(kKey), SaveResult::kStorageUnavailable);
  EXPECT_TRUE(StoredCredential().empty());
}

TEST_F(StyleAiKeyStoreTest, TreatsUndecryptableCiphertextAsNeedingReentry) {
  auto store = MakeStore();
  ASSERT_EQ(store->SaveKey(kKey), SaveResult::kSaved);
  base::DictValue corrupt = prefs_.GetDict(StyleAiKeyStore::kCredentialPref).Clone();
  corrupt.Set("ciphertext", "bm90IGNpcGhlcnRleHQ=");  // "not ciphertext"
  prefs_.SetDict(StyleAiKeyStore::kCredentialPref, std::move(corrupt));
  EXPECT_EQ(store->GetKeyState(), KeyState::kUndecryptable);
  EXPECT_FALSE(store->GetKeyForRequest().has_value());

  base::DictValue garbage;
  garbage.Set("ciphertext", "%%% not base64 %%%");
  prefs_.SetDict(StyleAiKeyStore::kCredentialPref, std::move(garbage));
  EXPECT_EQ(store->GetKeyState(), KeyState::kUndecryptable);
}

TEST_F(StyleAiKeyStoreTest, ReplacesTheKeyWhole) {
  auto store = MakeStore();
  ASSERT_EQ(store->SaveKey(kKey), SaveResult::kSaved);
  ASSERT_EQ(store->SaveKey(kOtherKey), SaveResult::kSaved);
  EXPECT_EQ(store->GetKeyForRequest(), std::string(kOtherKey));
  EXPECT_EQ(StoredCredential().find("OTHERKEY"), std::string::npos);
}

TEST_F(StyleAiKeyStoreTest, RemovesTheKey) {
  auto store = MakeStore();
  ASSERT_EQ(store->SaveKey(kKey), SaveResult::kSaved);
  store->RemoveKey();
  EXPECT_EQ(store->GetKeyState(), KeyState::kAbsent);
  EXPECT_FALSE(store->GetKeyForRequest().has_value());
  EXPECT_FALSE(store->GetSavedTime().has_value());
  EXPECT_TRUE(StoredCredential().empty());
}

TEST_F(StyleAiKeyStoreTest, RefusesRealKeysUnderAMockKeychain) {
  auto store = MakeStore(Environment::kMockKeychain);
  EXPECT_EQ(store->SaveKey(kKey), SaveResult::kInsecureStorageRefused);
  EXPECT_TRUE(StoredCredential().empty());
}

TEST_F(StyleAiKeyStoreTest, InsecureTestKeysAreUsableOnlyInTheTestEnvironment) {
  auto test_store = MakeStore(Environment::kInsecureTest);
  ASSERT_EQ(test_store->SaveKey(kKey), SaveResult::kSaved);
  EXPECT_TRUE(test_store->IsInsecureTestKey());
  EXPECT_EQ(test_store->GetKeyForRequest(), std::string(kKey));

  auto secure_store = MakeStore(Environment::kSecure);
  EXPECT_FALSE(secure_store->GetKeyForRequest().has_value());
  EXPECT_EQ(secure_store->GetKeyState(), KeyState::kUndecryptable);
}

TEST_F(StyleAiKeyStoreTest, ModelDefaultsAndAcceptsOnlyAllowedModels) {
  auto store = MakeStore();
  EXPECT_EQ(store->GetModel(), StyleAiKeyStore::kDefaultModel);
  EXPECT_EQ(store->GetModel(), "claude-opus-5-5");
  EXPECT_TRUE(store->SetModel("claude-sonnet-5-5"));
  EXPECT_EQ(store->GetModel(), "claude-sonnet-5-5");
  EXPECT_FALSE(store->SetModel("gpt-5"));
  EXPECT_FALSE(store->SetModel("claude-opus-5-5-20260401"));
  EXPECT_EQ(store->GetModel(), "claude-sonnet-5-5");
  // A value written behind the store's back falls back to the default.
  prefs_.SetString(StyleAiKeyStore::kModelPref, "anything-else");
  EXPECT_EQ(store->GetModel(), StyleAiKeyStore::kDefaultModel);
}

TEST(StyleAiSettingsEnvironmentTest, FollowsTheKeychainSwitches) {
  base::CommandLine plain(base::CommandLine::NO_PROGRAM);
  EXPECT_EQ(StyleAiSettings::EnvironmentFor(plain), Environment::kSecure);

  base::CommandLine mock(base::CommandLine::NO_PROGRAM);
  mock.AppendSwitch(os_crypt::switches::kUseMockKeychain);
  EXPECT_EQ(StyleAiSettings::EnvironmentFor(mock), Environment::kMockKeychain);

  base::CommandLine testing_only(base::CommandLine::NO_PROGRAM);
  testing_only.AppendSwitch(kStyleAiTestingSwitch);
  // The testing switch alone changes nothing: real secure storage is used.
  EXPECT_EQ(StyleAiSettings::EnvironmentFor(testing_only), Environment::kSecure);

  base::CommandLine both(base::CommandLine::NO_PROGRAM);
  both.AppendSwitch(os_crypt::switches::kUseMockKeychain);
  both.AppendSwitch(kStyleAiTestingSwitch);
#if defined(OFFICIAL_BUILD)
  EXPECT_EQ(StyleAiSettings::EnvironmentFor(both), Environment::kMockKeychain);
#else
  EXPECT_EQ(StyleAiSettings::EnvironmentFor(both), Environment::kInsecureTest);
#endif
}

}  // namespace
}  // namespace avora
