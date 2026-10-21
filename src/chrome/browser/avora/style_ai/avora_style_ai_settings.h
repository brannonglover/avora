// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_STYLE_AI_AVORA_STYLE_AI_SETTINGS_H_
#define CHROME_BROWSER_AVORA_STYLE_AI_AVORA_STYLE_AI_SETTINGS_H_

#include <memory>
#include <optional>
#include <string_view>

#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/supports_user_data.h"
#include "chrome/browser/avora/style_ai/avora_style_ai_key_store.h"

class Profile;

namespace base {
class CommandLine;
}

namespace os_crypt_async {
class Encryptor;
class OSCryptAsync;
}  // namespace os_crypt_async

namespace avora {

// Command-line switch, honoured only in non-official builds: accept a key
// under --use-mock-keychain, marked as an insecure test key that only the
// local fake provider will accept (Checkpoint 3B.2).
inline constexpr char kStyleAiTestingSwitch[] = "avora-style-ai-testing";

// Style Studio AI settings for one regular profile: the encrypted API key and
// the model. Off-the-record profiles have none, so AI generation is never
// available there.
class StyleAiSettings : public base::SupportsUserData::Data {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnStyleAiSettingsChanged() = 0;
  };

  // Set once from //chrome/browser, as for LiveFolderService.
  static void SetOSCryptAsync(os_crypt_async::OSCryptAsync* os_crypt_async);

  // Null for off-the-record profiles.
  static StyleAiSettings* GetForProfile(Profile* profile);

  // Which key-storage environment this process runs in.
  static StyleAiKeyStore::Environment EnvironmentFor(
      const base::CommandLine& command_line);

  explicit StyleAiSettings(Profile* profile);
  StyleAiSettings(const StyleAiSettings&) = delete;
  StyleAiSettings& operator=(const StyleAiSettings&) = delete;
  ~StyleAiSettings() override;

  StyleAiKeyStore& key_store() { return *key_store_; }
  const StyleAiKeyStore& key_store() const { return *key_store_; }

  // Saves, removes, or sets the model through the store and tells observers.
  StyleAiKeyStore::SaveResult SaveKey(std::string_view key);
  void RemoveKey();
  bool SetModel(std::string_view model);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 private:
  void OnEncryptorReady(scoped_refptr<os_crypt_async::Encryptor> encryptor);
  void NotifyChanged();

  std::unique_ptr<StyleAiKeyStore> key_store_;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<StyleAiSettings> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_STYLE_AI_AVORA_STYLE_AI_SETTINGS_H_
