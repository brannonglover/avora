// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_GITHUB_PROVIDER_H_
#define CHROME_BROWSER_AVORA_AVORA_GITHUB_PROVIDER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/avora/avora_live_folder_provider.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace avora {

class LiveFolderCredentialStore;

// Live Folder provider backed by GitHub's issue search API.
//
// One instance serves one Space, because the credential -- and therefore the
// account whose PRs are listed -- is Space-scoped.
//
// A single LiveFolderQuery usually needs several search requests: GitHub ANDs
// qualifiers within one query, so "authored by me OR awaiting my review" is
// not expressible as one search.  FetchItems() fans out, then merges and
// de-duplicates by PR id before reporting once.
class GitHubLiveFolderProvider : public LiveFolderProvider {
 public:
  GitHubLiveFolderProvider(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      LiveFolderCredentialStore* credential_store,
      const std::string& space_id);
  GitHubLiveFolderProvider(const GitHubLiveFolderProvider&) = delete;
  GitHubLiveFolderProvider& operator=(const GitHubLiveFolderProvider&) = delete;
  ~GitHubLiveFolderProvider() override;

  // Translates |query| into the minimal set of GitHub search strings covering
  // it.  Exposed for testing; callers should use FetchItems().
  static std::vector<std::string> BuildSearchQueriesForTesting(
      const LiveFolderQuery& query);

  // Parses one search API response body into items.  Exposed for testing.
  static std::vector<LiveFolderItem> ParseSearchResponseForTesting(
      const std::string& body);

  // LiveFolderProvider:
  LiveFolderProviderId id() const override;
  std::string display_name() const override;
  bool IsConnected() const override;
  void Connect(const std::string& credential,
               ConnectCallback callback) override;
  void Disconnect() override;
  std::string connected_account() const override;
  void FetchItems(const LiveFolderQuery& query,
                  FetchCallback callback) override;
  void CancelFetch() override;

 private:
  // One outstanding search request within a fan-out.
  struct PendingSearch {
    std::unique_ptr<network::SimpleURLLoader> loader;
  };

  void OnSearchResponse(size_t index,
                        std::optional<std::string> response_body);
  void OnValidateResponse(ConnectCallback callback,
                          const std::string& credential,
                          std::optional<std::string> response_body);

  // Reports the merged result once every fan-out request has finished.
  void MaybeFinishFetch();

  // Fails the whole fetch, cancelling any still-running requests.
  void FailFetch(LiveFolderFetchStatus status, const std::string& message);

  std::string CurrentToken() const;

  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  raw_ptr<LiveFolderCredentialStore> credential_store_;
  std::string space_id_;

  // Loader for Connect()'s validation call, independent of the fetch fan-out
  // so connecting does not cancel a running sync.
  std::unique_ptr<network::SimpleURLLoader> validate_loader_;

  std::vector<PendingSearch> searches_;
  size_t outstanding_ = 0;

  // Accumulated across the fan-out, keyed by external id so a PR matched by
  // two sub-queries is stored once.
  std::map<std::string, LiveFolderItem> merged_items_;

  bool include_drafts_ = true;

  FetchCallback fetch_callback_;

  base::WeakPtrFactory<GitHubLiveFolderProvider> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_GITHUB_PROVIDER_H_
