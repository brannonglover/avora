// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/style_ai/avora_style_ai_status.h"

namespace avora {

namespace {

const char* KeyStateName(StyleAiKeyStore::KeyState state) {
  switch (state) {
    case StyleAiKeyStore::KeyState::kAbsent:
      return "absent";
    case StyleAiKeyStore::KeyState::kPresent:
      return "present";
    case StyleAiKeyStore::KeyState::kUndecryptable:
      return "undecryptable";
    case StyleAiKeyStore::KeyState::kStorageNotReady:
      return "storage-not-ready";
  }
}

const char* EnvironmentName(StyleAiKeyStore::Environment environment) {
  switch (environment) {
    case StyleAiKeyStore::Environment::kSecure:
      return "secure";
    case StyleAiKeyStore::Environment::kMockKeychain:
      return "mock-keychain";
    case StyleAiKeyStore::Environment::kInsecureTest:
      return "insecure-test";
  }
}

}  // namespace

std::string SaveResultName(StyleAiKeyStore::SaveResult result) {
  switch (result) {
    case StyleAiKeyStore::SaveResult::kSaved:
      return "saved";
    case StyleAiKeyStore::SaveResult::kInvalidFormat:
      return "invalid-format";
    case StyleAiKeyStore::SaveResult::kStorageNotReady:
      return "storage-not-ready";
    case StyleAiKeyStore::SaveResult::kStorageUnavailable:
      return "storage-unavailable";
    case StyleAiKeyStore::SaveResult::kInsecureStorageRefused:
      return "insecure-storage-refused";
  }
}

base::DictValue StyleAiStatusForSettings(
    const StyleAiKeyStore* store,
    const std::optional<std::string>& last_result) {
  base::DictValue status;
  base::ListValue models;
  for (const std::string& model : StyleAiKeyStore::AllowedModels()) {
    models.Append(model);
  }
  status.Set("models", std::move(models));
  if (last_result) {
    status.Set("lastResult", *last_result);
  } else {
    status.Set("lastResult", base::Value());
  }
  if (!store) {
    status.Set("available", false);
    status.Set("reason", "off-the-record");
    status.Set("keyState", "absent");
    status.Set("insecureTestKey", false);
    status.Set("savedAt", base::Value());
    status.Set("environment", "secure");
    status.Set("model", StyleAiKeyStore::kDefaultModel);
    return status;
  }
  status.Set("available", true);
  status.Set("reason", base::Value());
  status.Set("keyState", KeyStateName(store->GetKeyState()));
  status.Set("insecureTestKey", store->IsInsecureTestKey());
  if (std::optional<base::Time> saved = store->GetSavedTime()) {
    status.Set("savedAt", saved->InMillisecondsFSinceUnixEpoch());
  } else {
    status.Set("savedAt", base::Value());
  }
  status.Set("environment", EnvironmentName(store->environment()));
  status.Set("model", store->GetModel());
  return status;
}

}  // namespace avora
