// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/style_ai/avora_style_ai_settings.h"

#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/os_crypt/common/os_crypt_switches.h"

namespace avora {

namespace {

constexpr char kUserDataKey[] = "avora_style_ai_settings";

// Set once from inside //chrome/browser (browser_view.cc), like
// LiveFolderService's: this target cannot reach the browser process.
os_crypt_async::OSCryptAsync* g_os_crypt_async = nullptr;

}  // namespace

// static
void StyleAiSettings::SetOSCryptAsync(
    os_crypt_async::OSCryptAsync* os_crypt_async) {
  g_os_crypt_async = os_crypt_async;
}

// static
StyleAiSettings* StyleAiSettings::GetForProfile(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }
  if (auto* existing =
          static_cast<StyleAiSettings*>(profile->GetUserData(kUserDataKey))) {
    return existing;
  }
  auto settings = std::make_unique<StyleAiSettings>(profile);
  StyleAiSettings* raw = settings.get();
  profile->SetUserData(kUserDataKey, std::move(settings));
  return raw;
}

// static
StyleAiKeyStore::Environment StyleAiSettings::EnvironmentFor(
    const base::CommandLine& command_line) {
  if (!command_line.HasSwitch(os_crypt::switches::kUseMockKeychain)) {
    return StyleAiKeyStore::Environment::kSecure;
  }
#if !defined(OFFICIAL_BUILD)
  if (command_line.HasSwitch(kStyleAiTestingSwitch)) {
    return StyleAiKeyStore::Environment::kInsecureTest;
  }
#endif
  return StyleAiKeyStore::Environment::kMockKeychain;
}

StyleAiSettings::StyleAiSettings(Profile* profile)
    : key_store_(std::make_unique<StyleAiKeyStore>(
          profile->GetPrefs(),
          EnvironmentFor(*base::CommandLine::ForCurrentProcess()))) {
  if (g_os_crypt_async) {
    g_os_crypt_async->GetInstance(base::BindOnce(
        &StyleAiSettings::OnEncryptorReady, weak_factory_.GetWeakPtr()));
  }
}

StyleAiSettings::~StyleAiSettings() = default;

StyleAiKeyStore::SaveResult StyleAiSettings::SaveKey(std::string_view key) {
  StyleAiKeyStore::SaveResult result = key_store_->SaveKey(key);
  NotifyChanged();
  return result;
}

void StyleAiSettings::RemoveKey() {
  key_store_->RemoveKey();
  NotifyChanged();
}

bool StyleAiSettings::SetModel(std::string_view model) {
  bool changed = key_store_->SetModel(model);
  NotifyChanged();
  return changed;
}

void StyleAiSettings::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void StyleAiSettings::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void StyleAiSettings::OnEncryptorReady(
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  key_store_->SetEncryptor(std::move(encryptor));
  NotifyChanged();
}

void StyleAiSettings::NotifyChanged() {
  for (Observer& observer : observers_) {
    observer.OnStyleAiSettingsChanged();
  }
}

}  // namespace avora
