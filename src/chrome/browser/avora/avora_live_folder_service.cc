// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_live_folder_service.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "chrome/browser/avora/avora_github_provider.h"
#include "chrome/browser/avora/avora_profile.h"
#include "chrome/browser/avora/avora_storage_partition.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/storage_partition.h"
#include "url/gurl.h"

namespace avora {

namespace {

constexpr char kUserDataKey[] = "avora_live_folder_service";

// Encrypted GitHub tokens from before Live Folders used the github.com
// session.  Nothing reads it; it is cleared so the tokens do not linger.
constexpr char kObsoleteCredentialsPref[] = "avora.live_folder_credentials";

std::string ProviderKey(const std::string& space_id,
                        LiveFolderProviderId provider) {
  return base::StrCat({space_id, "/", LiveFolderProviderIdToString(provider)});
}

}  // namespace

// static
void LiveFolderService::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kObsoleteCredentialsPref);
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

  PrefService* prefs = profile_->GetPrefs();
  if (prefs && prefs->FindPreference(kObsoleteCredentialsPref)) {
    prefs->ClearPref(kObsoleteCredentialsPref);
  }

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

content::StoragePartition* LiveFolderService::GetStoragePartitionForSpace(
    const std::string& space_id) {
  // Mirrors how a new tab picks its partition, so the folder sees exactly the
  // github.com session the Space's tabs do.
  const Space* space =
      space_manager_ ? space_manager_->GetSpaceById(space_id) : nullptr;
  BrowserProfileStore profile_store(profile_->GetPrefs());
  const BrowserProfile* browser_profile =
      space ? profile_store.GetProfileById(space->profile_id) : nullptr;
  if (!browser_profile) {
    browser_profile = profile_store.GetDefaultProfile();
  }
  if (!browser_profile || UsesDefaultPartition(*browser_profile)) {
    return profile_->GetDefaultStoragePartition();
  }
  return profile_->GetStoragePartition(
      GetPartitionConfigForProfile(profile_, *browser_profile));
}

LiveFolderProvider* LiveFolderService::GetOrCreateProvider(
    const std::string& space_id,
    LiveFolderProviderId provider_id) {
  if (space_id.empty() || !profile_) {
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
          GetStoragePartitionForSpace(space_id), space_id);
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

// ── Sign-in ─────────────────────────────────────────────────────────────────

GURL LiveFolderService::GetSignInUrl(LiveFolderProviderId provider_id) {
  if (!store_) {
    return GURL();
  }
  LiveFolderProvider* provider =
      GetOrCreateProvider(store_->active_space_id(), provider_id);
  return provider ? provider->sign_in_url() : GURL();
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

  // Always attempt the fetch: only the provider can tell whether the Space is
  // signed in, and it checks that before making any request.
  state.syncing = true;
  last_sync_started_[folder.id] = base::TimeTicks::Now();
  NotifySyncStateChanged(folder.id);

  provider->FetchItems(
      folder.query,
      base::BindOnce(&LiveFolderService::OnFetchComplete,
                     weak_factory_.GetWeakPtr(), space_id, folder.id,
                     folder.provider));
}

void LiveFolderService::OnFetchComplete(const std::string& space_id,
                                        const std::string& folder_id,
                                        LiveFolderProviderId provider_id,
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
  // store always targets whichever Space is active.  The switch's own sync
  // was turned away while this one ran, so run it now.
  if (store_->active_space_id() != space_id) {
    NotifySyncStateChanged(folder_id);
    ForceRefreshFolder(folder_id);
    return;
  }

  if (LiveFolderProvider* provider =
          GetOrCreateProvider(space_id, provider_id)) {
    state.connected = provider->IsConnected();
    state.account = provider->connected_account();
  }

  if (result.ok()) {
    store_->ReplaceItems(folder_id, result.items);
  } else if (result.status == LiveFolderFetchStatus::kNotConnected) {
    // Signing out on github.com must take the account's pull requests with
    // it; leaving them up would imply they are still being kept current.
    store_->ReplaceItems(folder_id, {});
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

  if (store_->WasFolderDismissed(
          LiveFolderStore::kGitHubPullRequestsFolderId)) {
    return;
  }

  if (store_->HasFolder(LiveFolderStore::kGitHubPullRequestsFolderId)) {
    // Being on github.com while the folder is signed out usually means the
    // user is signing in.  Re-checking is a local cookie read until the
    // session actually exists, so it is cheap to do on every visit.
    const std::string folder_id = LiveFolderStore::kGitHubPullRequestsFolderId;
    if (!GetSyncState(folder_id).connected) {
      ForceRefreshFolder(folder_id);
    }
    return;
  }

  // A Space already signed in to GitHub fills right away -- the "it just
  // works" case, with nothing to connect.
  const std::string folder_id = store_->EnsureGitHubPullRequestsFolder();
  if (!folder_id.empty()) {
    RefreshFolder(folder_id);
  }
}

// ── Space changes ───────────────────────────────────────────────────────────

void LiveFolderService::OnActiveSpaceChanged(const std::string& space_id) {
  RefreshActiveSpace();
}

}  // namespace avora
