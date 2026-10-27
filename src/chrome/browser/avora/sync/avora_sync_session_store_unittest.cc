// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_sync_session_store.h"

#include <memory>
#include <string>

#include "base/uuid.h"
#include "base/values.h"
#include "components/os_crypt/async/browser/test_utils.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace avora::sync {
namespace {

using Environment = SessionStore::Environment;
using SaveResult = SessionStore::SaveResult;

constexpr char kAccessToken[] = "eyJhbGciOiJIUzI1NiJ9.access.signature";
constexpr char kRefreshToken[] = "refresh-token-v1";

AuthSession MakeSession() {
  AuthSession session;
  session.access_token = kAccessToken;
  session.refresh_token = kRefreshToken;
  session.expires_at = base::Time::FromMillisecondsSinceUnixEpoch(1793000000000);
  session.user_id = "user-1";
  session.email = "me@example.com";
  return session;
}

class AvoraSyncSessionStoreTest : public testing::Test {
 protected:
  void SetUp() override {
    SessionStore::RegisterProfilePrefs(prefs_.registry());
  }

  std::unique_ptr<SessionStore> MakeStore(
      Environment environment = Environment::kSecure) {
    auto store = std::make_unique<SessionStore>(&prefs_, environment);
    store->SetEncryptor(encryptor_);
    return store;
  }

  std::string StoredSession() const {
    std::string out;
    for (auto [name, value] : prefs_.GetDict(SessionStore::kSessionPref)) {
      out += name + "=" + value.DebugString();
    }
    return out;
  }

  TestingPrefServiceSimple prefs_;
  scoped_refptr<os_crypt_async::Encryptor> encryptor_ =
      os_crypt_async::GetTestEncryptorForTesting();
};

TEST_F(AvoraSyncSessionStoreTest, PersistsEncryptedAndReadsBack) {
  auto store = MakeStore();
  EXPECT_FALSE(store->HasStoredSession());
  EXPECT_FALSE(store->Load());

  EXPECT_EQ(store->Save(MakeSession()), SaveResult::kPersisted);
  // Neither token is on disk in clear.
  EXPECT_FALSE(StoredSession().empty());
  EXPECT_EQ(StoredSession().find(kAccessToken), std::string::npos);
  EXPECT_EQ(StoredSession().find(kRefreshToken), std::string::npos);

  // A fresh store, as after a restart, reads it back.
  auto restarted = MakeStore();
  EXPECT_TRUE(restarted->HasStoredSession());
  std::optional<AuthSession> loaded = restarted->Load();
  ASSERT_TRUE(loaded);
  EXPECT_EQ(loaded->access_token, kAccessToken);
  EXPECT_EQ(loaded->refresh_token, kRefreshToken);
  EXPECT_EQ(loaded->expires_at, MakeSession().expires_at);
  EXPECT_EQ(loaded->user_id, "user-1");
  EXPECT_EQ(loaded->email, "me@example.com");
  EXPECT_EQ(restarted->GetEmail(), "me@example.com");
}

TEST_F(AvoraSyncSessionStoreTest, NothingReadableBeforeTheEncryptorArrives) {
  MakeStore()->Save(MakeSession());
  SessionStore waiting(&prefs_, Environment::kSecure);
  EXPECT_TRUE(waiting.HasStoredSession());
  EXPECT_FALSE(waiting.Load());
  // The email is there for the settings page meanwhile.
  EXPECT_EQ(waiting.GetEmail(), "me@example.com");
}

TEST_F(AvoraSyncSessionStoreTest, MockKeychainKeepsTheSessionInMemoryOnly) {
  auto store = MakeStore(Environment::kMockKeychain);
  EXPECT_EQ(store->Save(MakeSession()), SaveResult::kMemoryOnly);
  EXPECT_TRUE(StoredSession().empty());
  ASSERT_TRUE(store->Load());
  EXPECT_EQ(store->Load()->refresh_token, kRefreshToken);

  // Gone after a restart.
  EXPECT_FALSE(MakeStore(Environment::kMockKeychain)->Load());
}

TEST_F(AvoraSyncSessionStoreTest, UnavailableEncryptionKeepsSessionInMemory) {
  MakeStore()->Save(MakeSession());
  auto encryptor = os_crypt_async::GetTestEncryptorForTesting();
  encryptor->set_encryption_available_for_testing(false);
  SessionStore store(&prefs_, Environment::kSecure);
  store.SetEncryptor(encryptor);

  AuthSession rotated = MakeSession();
  rotated.refresh_token = "refresh-token-v2";
  EXPECT_EQ(store.Save(rotated), SaveResult::kMemoryOnly);
  // The older ciphertext does not outlive the session it belonged to.
  EXPECT_TRUE(StoredSession().empty());
  EXPECT_EQ(store.Load()->refresh_token, "refresh-token-v2");
}

TEST_F(AvoraSyncSessionStoreTest, UndecryptableSessionReadsAsSignedOut) {
  MakeStore()->Save(MakeSession());
  base::DictValue corrupt;
  corrupt.Set("ciphertext", "bm90IGNpcGhlcnRleHQ=");  // "not ciphertext"
  prefs_.SetDict(SessionStore::kSessionPref, std::move(corrupt));
  auto store = MakeStore();
  EXPECT_TRUE(store->HasStoredSession());
  EXPECT_FALSE(store->Load());
}

TEST_F(AvoraSyncSessionStoreTest, ClearForgetsSessionButKeepsDeviceId) {
  auto store = MakeStore();
  const std::string device_id = store->GetOrCreateDeviceId();
  store->Save(MakeSession());
  store->Clear();
  EXPECT_FALSE(store->HasStoredSession());
  EXPECT_FALSE(store->Load());
  EXPECT_EQ(store->GetEmail(), "");
  EXPECT_EQ(store->GetOrCreateDeviceId(), device_id);
}

TEST_F(AvoraSyncSessionStoreTest, DeviceIdIsAStableUuidUntilRotated) {
  auto store = MakeStore();
  const std::string id = store->GetOrCreateDeviceId();
  EXPECT_TRUE(base::Uuid::ParseLowercase(id).is_valid());
  EXPECT_EQ(MakeStore()->GetOrCreateDeviceId(), id);

  store->RotateDeviceId();
  const std::string rotated = store->GetOrCreateDeviceId();
  EXPECT_TRUE(base::Uuid::ParseLowercase(rotated).is_valid());
  EXPECT_NE(rotated, id);
}

}  // namespace
}  // namespace avora::sync
