// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_CREDENTIALS_H_
#define CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_CREDENTIALS_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "chrome/browser/avora/avora_live_folder.h"
#include "components/os_crypt/async/common/encryptor.h"

class PrefService;
class PrefRegistrySimple;

namespace avora {

// Stores Live Folder provider credentials, keyed by (Space, provider).
//
// Keyed by Space rather than by BrowserProfile so that two Spaces sharing a
// browser identity can still point at different accounts -- a work Space and a
// personal Space often share cookies but not the GitHub login you want to see
// PRs for.
//
// Tokens are encrypted before being written, so they are not sitting in
// cleartext in the Preferences JSON alongside ordinary settings.  This is the
// same protection Chromium gives saved passwords; it defends against casual
// disk inspection, not against code running as the user.
//
// The encryptor arrives asynchronously at startup and must be supplied by a
// caller that can reach the browser process, because this target is a
// dependency of //chrome/browser and cannot depend back on it.  Until it
// arrives the store reports no credentials rather than returning ciphertext.
class LiveFolderCredentialStore {
 public:
  // Dict[space_id -> Dict[provider -> Dict{token, account}]].
  static constexpr char kCredentialsPref[] = "avora.live_folder_credentials";

  explicit LiveFolderCredentialStore(PrefService* pref_service);
  LiveFolderCredentialStore(const LiveFolderCredentialStore&) = delete;
  LiveFolderCredentialStore& operator=(const LiveFolderCredentialStore&) =
      delete;
  ~LiveFolderCredentialStore();

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  // Supplies the encryptor once it is available.  Before this is called the
  // store behaves as though nothing is stored.
  void SetEncryptor(scoped_refptr<os_crypt_async::Encryptor> encryptor);

  // False until SetEncryptor() has been called.  Reads and writes are no-ops
  // while this is false.
  bool is_ready() const { return encryptor_ != nullptr; }

  // Decrypted token, or empty when absent or undecryptable.  An undecryptable
  // entry is treated as absent rather than as an error: that happens when the
  // profile moves machines, and the only recovery is to reconnect anyway.
  std::string GetToken(const std::string& space_id,
                       LiveFolderProviderId provider) const;

  // Account login associated with the stored token, for display.
  std::string GetAccount(const std::string& space_id,
                         LiveFolderProviderId provider) const;

  bool HasToken(const std::string& space_id,
                LiveFolderProviderId provider) const;

  void SetToken(const std::string& space_id,
                LiveFolderProviderId provider,
                const std::string& token,
                const std::string& account);

  void ClearToken(const std::string& space_id, LiveFolderProviderId provider);

  // Drops every credential belonging to a Space, for Space deletion.
  void ClearSpace(const std::string& space_id);

 private:
  // Returns the stored entry for (space, provider), or nullptr.
  const base::DictValue* FindEntry(const std::string& space_id,
                                   LiveFolderProviderId provider) const;

  raw_ptr<PrefService> pref_service_;
  bool pref_available_ = false;

  scoped_refptr<os_crypt_async::Encryptor> encryptor_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_LIVE_FOLDER_CREDENTIALS_H_
