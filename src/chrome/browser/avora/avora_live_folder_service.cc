// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_live_folder_service.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "chrome/browser/avora/avora_github_provider.h"
#include "chrome/browser/profiles/profile.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "content/public/browser/storage_partition.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace avora {

namespace {

constexpr char kUserDataKey[] = "avora_live_folder_service";

// Process-wide, set once from inside //chrome/browser.  A global rather than a
// constructor argument because services are created lazily by views, which
// cannot reach the browser process from this dependency level.
os_crypt_async::OSCryptAsync* g_os_crypt_async = nullptr;

std::string ProviderKey(const std::string& space_id,
                        LiveFolderProviderId provider) {
  return base::StrCat({space_id, "/", LiveFolderProviderIdToString(provider)});
}

}  // namespace

// static
void LiveFolderService::SetOSCryptAsync(
    os_crypt_async::OSCryptAsync* os_crypt_async) {
  g_os_crypt_async = os_crypt_async;
}

// static
LiveFolderService* LiveFolderService::GetForProfile(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }

  if (auto* existing = static_cast<LiveFolderService*>(
          profile->GetUserData(kUserDataKey))) {
    return existing;
  }

  auto service = std::make_unique<LiveFolderService>(profile);
  LiveFolderService* raw = service.get();
  profile->SetUserData(kUserDataKey, std::move(service));

  // Only now is the service reachable, so the first sync cannot recurse back
  // into GetForProfile() and build a second instance.
  raw->Initialize();
  return raw;
}

LiveFolderService::LiveFolderService(Profile* profile) : profile_(profile) {
  if (!profile_) {
    return;
  }

  url_loader_factory_ = profile_->GetDefaultStoragePartition()
                            ->GetURLLoaderFactoryForBrowserProcess();

  credentials_ =
      std::make_unique<LiveFolderCredentialStore>(profile_->GetPrefs());
  store_ = std::make_unique<LiveFolderStore>(profile_->GetPrefs());

  space_manager_ = std::make_unique<SpaceManager>(profile_->GetPrefs());
  space_manager_->AddObserver(this);
}

void LiveFolderService::Initialize() {
  if (!profile_) {
    return;
  }

  refresh_timer_.Start(
      FROM_HERE, kRefreshInterval,
      base::BindRepeating(&LiveFolderService::RefreshActiveSpace,
                          base::Unretained(this)));

  // Nothing can be decrypted until the encryptor lands, so the first sync
  // waits for it rather than running and concluding "not connected".
  if (g_os_crypt_async) {
    g_os_crypt_async->GetInstance(
        base::BindOnce(&LiveFolderService::OnEncryptorReady,
                       weak_factory_.GetWeakPtr()));
  }
}

void LiveFolderService::OnEncryptorReady(
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  if (credentials_) {
    credentials_->SetEncryptor(std::move(encryptor));
  }
  RefreshActiveSpace();
}

LiveFolderService::~LiveFolderService() {
  if (space_manager_) {
    space_manager_->RemoveObserver(this);
  }
}

void LiveFolderService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void LiveFolderService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

// ── Providers ───────────────────────────────────────────────────────────────

LiveFolderProvider* LiveFolderService::GetOrCreateProvider(
    const std::string& space_id,
    LiveFolderProviderId provider_id) {
  if (space_id.empty() || !url_loader_factory_) {
    return nullptr;
  }

  const std::string key = ProviderKey(space_id, provider_id);
  auto it = providers_.find(key);
  if (it != providers_.end()) {
    return it->second.get();
  }

  std::unique_ptr<LiveFolderProvider> provider;
  switch (provider_id) {
    case LiveFolderProviderId::kGitHub:
      provider = std::make_unique<GitHubLiveFolderProvider>(
          url_loader_factory_, credentials_.get(), space_id);
      break;
    case LiveFolderProviderId::kUnknown:
      return nullptr;
  }

  LiveFolderProvider* raw = provider.get();
  providers_[key] = std::move(provider);
  return raw;
}

// ── Sync state ──────────────────────────────────────────────────────────────

LiveFolderService::SyncState LiveFolderService::GetSyncState(
    const std::string& folder_id) const {
  const auto it = sync_states_.find(folder_id);
  if (it != sync_states_.end()) {
    return it->second;
  }
  return SyncState();
}

void LiveFolderService::NotifySyncStateChanged(const std::string& folder_id) {
  for (Observer& observer : observers_) {
    observer.OnLiveFolderSyncStateChanged(folder_id);
  }
}

// ── Connect / disconnect ────────────────────────────────────────────────────

void LiveFolderService::ConnectProvider(
    LiveFolderProviderId provider_id,
    const std::string& credential,
    LiveFolderProvider::ConnectCallback callback) {
  if (!store_) {
    std::move(callback).Run(false, "Live Folders are unavailable.");
    return;
  }

  const std::string space_id = store_->active_space_id();
  LiveFolderProvider* provider = GetOrCreateProvider(space_id, provider_id);
  if (!provider) {
    std::move(callback).Run(false, "Live Folders are unavailable.");
    return;
  }

  provider->Connect(
      credential,
      base::BindOnce(
          [](base::WeakPtr<LiveFolderService> service,
             LiveFolderProvider::ConnectCallback callback, bool success,
             const std::string& error) {
            if (service && success) {
              // A freshly connected account should populate immediately
              // rather than wait out the refresh interval.
              const std::string folder_id =
                  service->store_->EnsureGitHubPullRequestsFolder();
              service->ForceRefreshFolder(folder_id);
            }
            std::move(callback).Run(success, error);
          },
          weak_factory_.GetWeakPtr(), std::move(callback)));
}

void LiveFolderService::DisconnectProvider(LiveFolderProviderId provider_id) {
  if (!store_) {
    return;
  }
  const std::string space_id = store_->active_space_id();
  if (LiveFolderProvider* provider =
          GetOrCreateProvider(space_id, provider_id)) {
    provider->Disconnect();
  }

  for (const LiveFolder& folder : store_->GetFolders()) {
    if (folder.provider != provider_id) {
      continue;
    }
    // Leaving the rows up would imply they are still being kept current.
    store_->ReplaceItems(folder.id, {});
    sync_states_.erase(folder.id);
    NotifySyncStateChanged(folder.id);
  }
}

// ── Refresh ─────────────────────────────────────────────────────────────────

void LiveFolderService::RefreshActiveSpace() {
  if (!store_) {
    return;
  }
  for (const LiveFolder& folder : store_->GetFolders()) {
    SyncFolder(folder, /*force=*/false);
  }
}

void LiveFolderService::RefreshFolder(const std::string& folder_id) {
  if (!store_) {
    return;
  }
  if (std::optional<LiveFolder> folder = store_->GetFolderById(folder_id)) {
    SyncFolder(*folder, /*force=*/false);
  }
}

void LiveFolderService::ForceRefreshFolder(const std::string& folder_id) {
  if (!store_) {
    return;
  }
  if (std::optional<LiveFolder> folder = store_->GetFolderById(folder_id)) {
    SyncFolder(*folder, /*force=*/true);
  }
}

void LiveFolderService::SyncFolder(const LiveFolder& folder, bool force) {
  if (!store_ || folder.id.empty()) {
    return;
  }

  SyncState& state = sync_states_[folder.id];

  // A second fetch would cancel the first and restart the clock, so an
  // in-flight sync always wins over a new request.
  if (state.syncing) {
    return;
  }

  if (!force) {
    const auto it = last_sync_started_.find(folder.id);
    if (it != last_sync_started_.end() &&
        base::TimeTicks::Now() - it->second < kMinSyncSpacing) {
      return;
    }
  }

  const std::string space_id = store_->active_space_id();
  LiveFolderProvider* provider =
      GetOrCreateProvider(space_id, folder.provider);
  if (!provider) {
    return;
  }

  state.connected = provider->IsConnected();
  state.account = provider->connected_account();

  if (!state.connected) {
    state.syncing = false;
    state.last_status = LiveFolderFetchStatus::kNotConnected;
    state.last_error.clear();
    NotifySyncStateChanged(folder.id);
    return;
  }

  state.syncing = true;
  last_sync_started_[folder.id] = base::TimeTicks::Now();
  NotifySyncStateChanged(folder.id);

  provider->FetchItems(
      folder.query,
      base::BindOnce(&LiveFolderService::OnFetchComplete,
                     weak_factory_.GetWeakPtr(), space_id, folder.id));
}

void LiveFolderService::OnFetchComplete(const std::string& space_id,
                                        const std::string& folder_id,
                                        LiveFolderFetchResult result) {
  SyncState& state = sync_states_[folder_id];
  state.syncing = false;
  state.last_status = result.status;
  state.last_error = result.error_message;

  if (!store_) {
    return;
  }

  // The user may have switched Spaces while this was in flight.  Writing now
  // would put one Space's pull requests into another's folder, because the
  // store always targets whichever Space is active.
  if (store_->active_space_id() != space_id) {
    NotifySyncStateChanged(folder_id);
    return;
  }

  if (result.ok()) {
    store_->ReplaceItems(folder_id, result.items);
  }

  NotifySyncStateChanged(folder_id);
}

// ── Provider discovery ──────────────────────────────────────────────────────

void LiveFolderService::OnHostVisited(const std::string& host) {
  if (!store_) {
    return;
  }

  // Only the canonical hosts, not every subdomain: raw.githubusercontent.com
  // and gist.github.com are not signals that the user works in pull requests.
  const bool is_github = host == "github.com" || host == "www.github.com";
  if (!is_github) {
    return;
  }

  if (store_->HasFolder(LiveFolderStore::kGitHubPullRequestsFolderId) ||
      store_->WasFolderDismissed(
          LiveFolderStore::kGitHubPullRequestsFolderId)) {
    return;
  }

  const std::string folder_id = store_->EnsureGitHubPullRequestsFolder();
  if (!folder_id.empty()) {
    // Nothing will load until the user connects an account, but syncing now
    // populates the folder immediately in the case where a credential already
    // exists for this Space.
    RefreshFolder(folder_id);
  }
}

// ── Space changes ───────────────────────────────────────────────────────────

void LiveFolderService::OnActiveSpaceChanged(const std::string& space_id) {
  RefreshActiveSpace();
}

}  // namespace avora
