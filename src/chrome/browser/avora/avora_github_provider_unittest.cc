// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_github_provider.h"

#include <optional>
#include <string>

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace avora {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::SizeIs;

// Trimmed from a real github.com/search response, keeping only the fields the
// parser reads plus one it ignores.
constexpr char kSignedInResponse[] = R"({
  "payload": {"blackbirdSearchRoute": {
    "logged_in": true,
    "results": [
      {
        "id": "5056423329",
        "number": 14858,
        "state": "open",
        "reviewable_state": "ready",
        "merged": false,
        "author_name": "octocat",
        "hl_title": "Fix <em>delete</em> 5&#x2F;12 &amp; more",
        "created": "2026-10-28T13:26:30.000-06:00",
        "num_comments": 3,
        "repo": {"repository": {"owner_login": "cli", "name": "cli"}}
      },
      {
        "id": "4444217230",
        "number": 14355,
        "state": "draft",
        "reviewable_state": "draft",
        "merged": false,
        "hl_title": "Draft work",
        "repo": {"repository": {"owner_login": "cli", "name": "cli"}}
      },
      {
        "id": "missing-repo",
        "number": 1,
        "hl_title": "Dropped"
      }
    ]
  }}
})";

TEST(GitHubLiveFolderProviderTest, ParsesSignedInResults) {
  const std::optional<GitHubLiveFolderProvider::SearchResponse> response =
      GitHubLiveFolderProvider::ParseSearchResponseForTesting(
          kSignedInResponse);
  ASSERT_TRUE(response);
  EXPECT_TRUE(response->logged_in);
  ASSERT_THAT(response->items, SizeIs(2));

  const LiveFolderItem& first = response->items[0];
  EXPECT_EQ(first.external_id, "5056423329");
  EXPECT_EQ(first.title, "Fix delete 5/12 & more");
  EXPECT_EQ(first.url, "https://github.com/cli/cli/pull/14858");
  EXPECT_EQ(first.metadata.repo, "cli/cli");
  EXPECT_EQ(first.metadata.number, 14858);
  EXPECT_EQ(first.metadata.author, "octocat");
  EXPECT_EQ(first.status, LiveFolderItemStatus::kOpen);
  EXPECT_FALSE(first.updated_at.is_null());

  EXPECT_EQ(response->items[1].status, LiveFolderItemStatus::kDraft);
}

TEST(GitHubLiveFolderProviderTest, ReportsSignedOutSession) {
  const std::optional<GitHubLiveFolderProvider::SearchResponse> response =
      GitHubLiveFolderProvider::ParseSearchResponseForTesting(
          R"({"payload": {"blackbirdSearchRoute":
                {"logged_in": false, "results": []}}})");
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->logged_in);
  EXPECT_THAT(response->items, IsEmpty());
}

TEST(GitHubLiveFolderProviderTest, RejectsNonSearchBodies) {
  EXPECT_FALSE(GitHubLiveFolderProvider::ParseSearchResponseForTesting(
      "<!DOCTYPE html><html></html>"));
  EXPECT_FALSE(
      GitHubLiveFolderProvider::ParseSearchResponseForTesting(R"({"a": 1})"));
}

TEST(GitHubLiveFolderProviderTest, DefaultQueryCollapsesToTwoSearches) {
  EXPECT_THAT(
      GitHubLiveFolderProvider::BuildSearchQueriesForTesting(LiveFolderQuery()),
      ElementsAre("is:pr is:open archived:false involves:@me",
                  "is:pr is:open archived:false user-review-requested:@me"));
}

}  // namespace
}  // namespace avora
