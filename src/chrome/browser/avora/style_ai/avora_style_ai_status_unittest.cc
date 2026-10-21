// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/style_ai/avora_style_ai_status.h"

#include <set>
#include <string>

#include "base/json/json_writer.h"
#include "components/os_crypt/async/browser/test_utils.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace avora {
namespace {

constexpr char kKey[] = "sk-ant-api03-STATUSKEYabcdefghijklmnopqrstuvwxyz01";

std::set<std::string> Keys(const base::DictValue& dict) {
  std::set<std::string> keys;
  for (auto [name, value] : dict) {
    keys.insert(name);
  }
  return keys;
}

class StyleAiStatusTest : public testing::Test {
 protected:
  void SetUp() override {
    StyleAiKeyStore::RegisterProfilePrefs(prefs_.registry());
    store_ = std::make_unique<StyleAiKeyStore>(
        &prefs_, StyleAiKeyStore::Environment::kSecure);
    store_->SetEncryptor(os_crypt_async::GetTestEncryptorForTesting());
  }

  TestingPrefServiceSimple prefs_;
  std::unique_ptr<StyleAiKeyStore> store_;
};

TEST_F(StyleAiStatusTest, NeverCarriesTheKey) {
  ASSERT_EQ(store_->SaveKey(kKey), StyleAiKeyStore::SaveResult::kSaved);
  const base::DictValue status = StyleAiStatusForSettings(store_.get(), "saved");
  const std::string json = base::WriteJson(status).value_or("");
  EXPECT_EQ(json.find(kKey), std::string::npos);
  EXPECT_EQ(json.find("STATUSKEY"), std::string::npos);
  EXPECT_EQ(json.find("sk-ant"), std::string::npos);
  // Nor the ciphertext.
  const std::string* ciphertext =
      prefs_.GetDict(StyleAiKeyStore::kCredentialPref).FindString("ciphertext");
  ASSERT_TRUE(ciphertext);
  EXPECT_EQ(json.find(*ciphertext), std::string::npos);
  // Exactly the documented fields, so a new one cannot slip in unnoticed.
  EXPECT_EQ(Keys(status),
            (std::set<std::string>{"available", "reason", "keyState",
                                   "insecureTestKey", "savedAt", "environment",
                                   "model", "models", "lastResult"}));
}

TEST_F(StyleAiStatusTest, DescribesAStoredKey) {
  ASSERT_EQ(store_->SaveKey(kKey), StyleAiKeyStore::SaveResult::kSaved);
  const base::DictValue status = StyleAiStatusForSettings(store_.get(), "saved");
  EXPECT_EQ(status.FindBool("available"), true);
  EXPECT_EQ(*status.FindString("keyState"), "present");
  EXPECT_TRUE(status.FindDouble("savedAt").has_value());
  EXPECT_EQ(*status.FindString("model"), "claude-opus-5-5");
  EXPECT_EQ(*status.FindString("environment"), "secure");
  EXPECT_EQ(*status.FindString("lastResult"), "saved");
  EXPECT_EQ(status.FindList("models")->size(), 3u);
}

TEST_F(StyleAiStatusTest, DescribesNoKey) {
  const base::DictValue status = StyleAiStatusForSettings(store_.get(), std::nullopt);
  EXPECT_EQ(*status.FindString("keyState"), "absent");
  EXPECT_TRUE(status.Find("savedAt")->is_none());
  EXPECT_TRUE(status.Find("lastResult")->is_none());
}

TEST_F(StyleAiStatusTest, IsUnavailableOffTheRecord) {
  const base::DictValue status = StyleAiStatusForSettings(nullptr, std::nullopt);
  EXPECT_EQ(status.FindBool("available"), false);
  EXPECT_EQ(*status.FindString("reason"), "off-the-record");
}

TEST(StyleAiSaveResultTest, NamesEveryResult) {
  using R = StyleAiKeyStore::SaveResult;
  EXPECT_EQ(SaveResultName(R::kSaved), "saved");
  EXPECT_EQ(SaveResultName(R::kInvalidFormat), "invalid-format");
  EXPECT_EQ(SaveResultName(R::kStorageNotReady), "storage-not-ready");
  EXPECT_EQ(SaveResultName(R::kStorageUnavailable), "storage-unavailable");
  EXPECT_EQ(SaveResultName(R::kInsecureStorageRefused),
            "insecure-storage-refused");
}

}  // namespace
}  // namespace avora
