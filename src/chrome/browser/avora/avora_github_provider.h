// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_GITHUB_PROVIDER_H_
#define CHROME_BROWSER_AVORA_AVORA_GITHUB_PROVIDER_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/avora/avora_live_folder_provider.h"
#include "net/cookies/canonical_cookie.h"

namespace content {
class StoragePartition;
}  // namespace content

namespace network {
class SimpleURLLoader;
}  // namespace network

namespace avora {

// Live Folder provider backed by github.com's own pull request search.
//
// There is no token to connect.  Requests carry the cookies of the Space's
// storage partition, so the folder lists pull requests for whichever account
// the Space is signed in to on github.com -- the same session its tabs use.
// Signing in on the site is what makes the folder fill; signing out empties
// it.
//
// That rules out api.github.com, which ignores session cookies.  Instead this
// asks github.com/search for JSON, which is what the site's own search page
// renders from, and which reports whether the session was signed in.
//
// One instance serves one Space, because the session -- and therefore the
// account whose PRs are listed -- is Space-scoped.
//
// A single LiveFolderQuery usually needs several searches: GitHub ANDs
// qualifiers within one query, so "authored by me OR awaiting my review" is
// not expressible as one search.  FetchItems() fans out, then merges and
// de-duplicates by PR id before reporting once.
class GitHubLiveFolderProvider : public LiveFolderProvider {
 public:
  // The parsed body of one search response.
  struct SearchResponse {
    // False when github.com answered as though nobody were signed in, which is
    // how an expired session shows up.
    bool logged_in = false;
    std::vector<LiveFolderItem> items;

    // Organisations that enforce SAML single sign-on and whose pull requests
    // github.com left out because this session has not signed in to them.
    std::vector<std::string> protected_orgs;
  };

  // |storage_partition| is the Space's partition and must outlive this
  // provider; partitions live as long as their BrowserContext.
  GitHubLiveFolderProvider(content::StoragePartition* storage_partition,
                           const std::string& space_id);
  GitHubLiveFolderProvider(const GitHubLiveFolderProvider&) = delete;
  GitHubLiveFolderProvider& operator=(const GitHubLiveFolderProvider&) = delete;
  ~GitHubLiveFolderProvider() override;

  // Translates |query| into the minimal set of GitHub search strings covering
  // it.  Exposed for testing; callers should use FetchItems().
  static std::vector<std::string> BuildSearchQueriesForTesting(
      const LiveFolderQuery& query);

  // Parses one search response body.  Returns nullopt when the body is not
  // the search JSON at all -- an HTML error page, say.  Exposed for testing.
  static std::optional<SearchResponse> ParseSearchResponseForTesting(
      const std::string& body);

  // LiveFolderProvider:
  LiveFolderProviderId id() const override;
  std::string display_name() const override;
  bool IsConnected() const override;
  std::string connected_account() const override;
  GURL sign_in_url() const override;
  void FetchItems(const LiveFolderQuery& query,
                  FetchCallback callback) override;
  void CancelFetch() override;

 private:
  // One outstanding search request within a fan-out.
  struct PendingSearch {
    std::unique_ptr<network::SimpleURLLoader> loader;
  };

  // Checks the partition's github.com cookies before spending any requests:
  // a Space that never signed in to GitHub should cost nothing every refresh.
  void OnCookiesRead(std::vector<std::string> queries,
                     const net::CookieAccessResultList& cookies,
                     const net::CookieAccessResultList& excluded_cookies);

  void StartSearches(const std::vector<std::string>& queries);

  void OnSearchResponse(size_t index, std::optional<std::string> response_body);

  // Reports the merged result once every fan-out request has finished.
  void MaybeFinishFetch();

  // Fails the whole fetch, cancelling any still-running requests.
  void FailFetch(LiveFolderFetchStatus status, const std::string& message);

  // Records that the session is signed out and reports kNotConnected.
  void ReportSignedOut();

  raw_ptr<content::StoragePartition> storage_partition_;
  std::string space_id_;

  // What the most recent fetch observed.  Signed out until proven otherwise,
  // so a Space that never visited GitHub shows the sign-in prompt.
  bool signed_in_ = false;
  std::string account_;

  std::vector<PendingSearch> searches_;
  size_t outstanding_ = 0;

  // Accumulated across the fan-out, keyed by external id so a PR matched by
  // two sub-queries is stored once.
  std::map<std::string, LiveFolderItem> merged_items_;

  // Likewise accumulated, since each sub-query reports the orgs it hid.
  std::set<std::string> protected_orgs_;

  bool include_drafts_ = true;

  FetchCallback fetch_callback_;

  // Invalidated by CancelFetch() so a cookie read from a superseded fetch
  // cannot start searches for it.
  base::WeakPtrFactory<GitHubLiveFolderProvider> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_GITHUB_PROVIDER_H_
