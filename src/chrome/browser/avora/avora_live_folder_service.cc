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

std::string FolderKey(const std::string& space_id,
                      const std::string& folder_id) {
  return base::StrCat({space_id, "/", folder_id});
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

  if (auto* existing =
          static_cast<LiveFolderService*>(profile->GetUserData(kUserDataKey))) {
    return existing;
  }

  auto service = std::make_unique<LiveFolderService>(profile);
  LiveFolderService* raw = service.get();
  profile->SetUserData(kUserDataKey, std::move(service));

  // Only now is the service reachable, so anything the first refresh calls
  // back into cannot recurse into GetForProfile() and build a second instance.
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

  store_ = std::make_unique<LiveFolderStore>(prefs);
  space_manager_ = std::make_unique<SpaceManager>(prefs);
}

void LiveFolderService::Initialize() {
  if (!profile_) {
    return;
  }

  // Nothing syncs until a view watches a Space; the timer only keeps the
  // watched ones current.
  refresh_timer_.Start(
      FROM_HERE, kRefreshInterval,
      base::BindRepeating(&LiveFolderService::RefreshWatchedSpaces,
                          base::Unretained(this)));
}

LiveFolderService::~LiveFolderService() = default;

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
    const std::string& space_id,
    const std::string& folder_id) const {
  const auto it = sync_states_.find(FolderKey(space_id, folder_id));
  if (it != sync_states_.end()) {
    return it->second;
  }
  return SyncState();
}

void LiveFolderService::NotifySyncStateChanged(const std::string& space_id,
                                               const std::string& folder_id) {
  for (Observer& observer : observers_) {
    observer.OnLiveFolderSyncStateChanged(space_id, folder_id);
  }
}

// ── Sign-in ─────────────────────────────────────────────────────────────────

GURL LiveFolderService::GetSignInUrl(const std::string& space_id,
                                     LiveFolderProviderId provider_id) {
  LiveFolderProvider* provider = GetOrCreateProvider(space_id, provider_id);
  return provider ? provider->sign_in_url() : GURL();
}

// ── Watched Spaces ──────────────────────────────────────────────────────────

void LiveFolderService::WatchSpace(const std::string& space_id) {
  if (space_id.empty()) {
    return;
  }
  if (++watched_spaces_[space_id] == 1) {
    RefreshSpace(space_id);
  }
}

void LiveFolderService::UnwatchSpace(const std::string& space_id) {
  auto it = watched_spaces_.find(space_id);
  if (it == watched_spaces_.end()) {
    return;
  }
  if (--it->second <= 0) {
    watched_spaces_.erase(it);
  }
}

void LiveFolderService::RefreshWatchedSpaces() {
  // Copied first: a sync that completes synchronously could notify a view
  // that changes what it watches.
  std::vector<std::string> space_ids;
  for (const auto& [space_id, count] : watched_spaces_) {
    space_ids.push_back(space_id);
  }
  for (const std::string& space_id : space_ids) {
    RefreshSpace(space_id);
  }
}

// ── Refresh ─────────────────────────────────────────────────────────────────

void LiveFolderService::RefreshSpace(const std::string& space_id) {
  if (!store_) {
    return;
  }
  for (const LiveFolder& folder : store_->GetFolders(space_id)) {
    SyncFolder(space_id, folder, /*force=*/false);
  }
}

void LiveFolderService::RefreshFolder(const std::string& space_id,
                                      const std::string& folder_id) {
  if (!store_) {
    return;
  }
  if (std::optional<LiveFolder> folder =
          store_->GetFolderById(space_id, folder_id)) {
    SyncFolder(space_id, *folder, /*force=*/false);
  }
}

void LiveFolderService::ForceRefreshFolder(const std::string& space_id,
                                           const std::string& folder_id) {
  if (!store_) {
    return;
  }
  if (std::optional<LiveFolder> folder =
          store_->GetFolderById(space_id, folder_id)) {
    SyncFolder(space_id, *folder, /*force=*/true);
  }
}

void LiveFolderService::SyncFolder(const std::string& space_id,
                                   const LiveFolder& folder,
                                   bool force) {
  if (!store_ || space_id.empty() || folder.id.empty()) {
    return;
  }

  const std::string key = FolderKey(space_id, folder.id);
  SyncState& state = sync_states_[key];

  // A second fetch would cancel the first and restart the clock, so an
  // in-flight sync always wins over a new request.
  if (state.syncing) {
    return;
  }

  if (!force) {
    const auto it = last_sync_started_.find(key);
    if (it != last_sync_started_.end() &&
        base::TimeTicks::Now() - it->second < kMinSyncSpacing) {
      return;
    }
  }

  LiveFolderProvider* provider = GetOrCreateProvider(space_id, folder.provider);
  if (!provider) {
    return;
  }

  // Always attempt the fetch: only the provider can tell whether the Space is
  // signed in, and it checks that before making any request.
  state.syncing = true;
  last_sync_started_[key] = base::TimeTicks::Now();
  NotifySyncStateChanged(space_id, folder.id);

  provider->FetchItems(folder.query,
                       base::BindOnce(&LiveFolderService::OnFetchComplete,
                                      weak_factory_.GetWeakPtr(), space_id,
                                      folder.id, folder.provider));
}

void LiveFolderService::OnFetchComplete(const std::string& space_id,
                                        const std::string& folder_id,
                                        LiveFolderProviderId provider_id,
                                        LiveFolderFetchResult result) {
  SyncState& state = sync_states_[FolderKey(space_id, folder_id)];
  state.syncing = false;
  state.last_status = result.status;
  state.last_error = result.error_message;

  if (LiveFolderProvider* provider =
          GetOrCreateProvider(space_id, provider_id)) {
    state.connected = provider->IsConnected();
    state.account = provider->connected_account();
  }

  if (!store_) {
    return;
  }

  if (result.ok()) {
    state.protected_sources = std::move(result.protected_sources);
    store_->ReplaceItems(space_id, folder_id, result.items);
  } else if (result.status == LiveFolderFetchStatus::kNotConnected) {
    // Signing out on github.com must take the account's pull requests with
    // it; leaving them up would imply they are still being kept current.
    state.protected_sources.clear();
    store_->ReplaceItems(space_id, folder_id, {});
  }

  NotifySyncStateChanged(space_id, folder_id);
}

// ── Provider discovery ──────────────────────────────────────────────────────

void LiveFolderService::OnHostVisited(const std::string& space_id,
                                      const std::string& host) {
  if (!store_ || space_id.empty()) {
    return;
  }

  // Only the canonical hosts, not every subdomain: raw.githubusercontent.com
  // and gist.github.com are not signals that the user works in pull requests.
  const bool is_github = host == "github.com" || host == "www.github.com";
  if (!is_github) {
    return;
  }

  const std::string folder_id = LiveFolderStore::kGitHubPullRequestsFolderId;
  if (store_->WasFolderDismissed(space_id, folder_id)) {
    return;
  }

  if (store_->HasFolder(space_id, folder_id)) {
    const SyncState state = GetSyncState(space_id, folder_id);
    if (!state.connected) {
      // Being on github.com while the folder is signed out usually means the
      // user is signing in.  Re-checking is a local cookie read until the
      // session actually exists, so it is cheap to do on every visit.
      ForceRefreshFolder(space_id, folder_id);
    } else if (!state.protected_sources.empty()) {
      // Likely completing an organisation's single sign-on.  That re-check
      // costs real searches, so it keeps to the usual spacing.
      RefreshFolder(space_id, folder_id);
    }
    return;
  }

  // A Space already signed in to GitHub fills right away -- the "it just
  // works" case, with nothing to connect.
  if (!store_->EnsureGitHubPullRequestsFolder(space_id).empty()) {
    RefreshFolder(space_id, folder_id);
  }
}

}  // namespace avora
