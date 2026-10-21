// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_STYLE_AI_AVORA_STYLE_AI_KEY_STORE_H_
#define CHROME_BROWSER_AVORA_STYLE_AI_AVORA_STYLE_AI_KEY_STORE_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/time/time.h"
#include "components/os_crypt/async/common/encryptor.h"

class PrefService;
class PrefRegistrySimple;

namespace avora {

// The Anthropic API key Style Studio uses, and the model it asks for.
//
// The key is encrypted with OSCryptAsync before it is written; the pref holds
// only Base64 ciphertext. On macOS the encryption key is derived from the
// "Avora Safe Storage" Keychain item, the same protection as saved passwords
// and Live Folders tokens: it defends against disk inspection, not against
// code running as the user. Nothing here ever stores, returns, or logs the key
// in clear except GetKeyForRequest(), which only the browser-process AI
// service calls, and only to build one request.
//
// See docs/style-intelligence/style-studio-checkpoint-3a-ai-architecture.md §9.2.
class StyleAiKeyStore {
 public:
  // Dict{ciphertext: Base64, saved_at: int64 (ms since the Unix epoch),
  // insecure_test: bool}. Not syncable.
  static constexpr char kCredentialPref[] = "avora.style_ai.credential";
  static constexpr char kModelPref[] = "avora.style_ai.model";

  // Verified against Anthropic's model list (2026-10-06); re-verified live with
  // GET /v1/models/{id} when a key is saved (Checkpoint 3B.2).
  static constexpr char kDefaultModel[] = "claude-opus-5-5";

  // Where the key may come from.
  enum class Environment {
    // Real OS secure storage.
    kSecure,
    // --use-mock-keychain: the "secure" storage is a fake keychain whose key
    // protects nothing. Real keys are refused.
    kMockKeychain,
    // A non-official build run with the Style Studio AI test switch: keys are
    // accepted under the mock keychain, marked insecure_test, and are usable
    // only against the local fake provider (Checkpoint 3B.2).
    kInsecureTest,
  };

  enum class SaveResult {
    kSaved,
    // Not shaped like an API key (see IsWellFormedKey).
    kInvalidFormat,
    // The encryptor has not arrived yet.
    kStorageNotReady,
    // OS secure storage is unavailable (for example Keychain access was
    // denied), so the key was not saved.
    kStorageUnavailable,
    // The browser runs with a mock keychain; a real key would be unprotected.
    kInsecureStorageRefused,
  };

  enum class KeyState {
    kAbsent,
    kPresent,
    // Stored, but cannot be decrypted here (profile moved machines, Keychain
    // item reset). The user must enter the key again.
    kUndecryptable,
    // The encryptor has not arrived yet, so nothing can be said.
    kStorageNotReady,
  };

  StyleAiKeyStore(PrefService* pref_service, Environment environment);
  StyleAiKeyStore(const StyleAiKeyStore&) = delete;
  StyleAiKeyStore& operator=(const StyleAiKeyStore&) = delete;
  ~StyleAiKeyStore();

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  // Printable ASCII without whitespace, 20 to 256 characters. A format check
  // only: whether the provider accepts the key is checked in 3B.2.
  static bool IsWellFormedKey(std::string_view key);

  static const std::vector<std::string>& AllowedModels();

  // Supplies the encryptor once it is available. Before this is called the
  // store saves nothing and reports kStorageNotReady.
  void SetEncryptor(scoped_refptr<os_crypt_async::Encryptor> encryptor);
  bool is_ready() const { return encryptor_ != nullptr; }

  Environment environment() const { return environment_; }

  // Encrypts and stores `key`, replacing any key stored before. On any failure
  // nothing is written and an existing key is left as it was.
  SaveResult SaveKey(std::string_view key);

  // Deletes the stored key.
  void RemoveKey();

  KeyState GetKeyState() const;

  // When the stored key was saved, if one is stored.
  std::optional<base::Time> GetSavedTime() const;

  // Whether the stored key was saved under the insecure test environment.
  bool IsInsecureTestKey() const;

  // The decrypted key, for building one provider request. Callers must not
  // keep, log, or pass it anywhere but the request's auth header.
  std::optional<std::string> GetKeyForRequest() const;

  // The model to use: the stored one if it is allowed, else kDefaultModel.
  std::string GetModel() const;

  // Stores `model` if it is one of AllowedModels(). Returns whether it was.
  bool SetModel(std::string_view model);

 private:
  raw_ptr<PrefService> pref_service_;
  const Environment environment_;
  scoped_refptr<os_crypt_async::Encryptor> encryptor_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_STYLE_AI_AVORA_STYLE_AI_KEY_STORE_H_
