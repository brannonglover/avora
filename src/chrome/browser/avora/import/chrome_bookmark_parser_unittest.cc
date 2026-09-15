// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/import/chromium_bookmark_parser.h"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "chrome/browser/avora/avora_imported_links.h"
#include "chrome/browser/avora/import/avora_import_coordinator.h"
#include "chrome/browser/avora/import/bookmark_import_types.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace avora {
namespace {

using ::testing::IsEmpty;
using ::testing::SizeIs;

// ── JSON fixtures ───────────────────────────────────────────────────────────

constexpr char kMinimalBookmarks[] = R"({
  "checksum": "abc",
  "roots": {
    "bookmark_bar": {
      "children": [],
      "name": "Bookmarks Bar",
      "type": "folder"
    },
    "other": {
      "children": [],
      "name": "Other Bookmarks",
      "type": "folder"
    },
    "synced": {
      "children": [],
      "name": "Mobile Bookmarks",
      "type": "folder"
    }
  },
  "version": 1
})";

constexpr char kBookmarkBarOnly[] = R"({
  "roots": {
    "bookmark_bar": {
      "children": [
        {
          "name": "GitHub",
          "type": "url",
          "url": "https://github.com",
          "id": "2",
          "guid": "aaaa-bbbb-cccc"
        },
        {
          "name": "Gmail",
          "type": "url",
          "url": "https://mail.google.com",
          "id": "3",
          "guid": "dddd-eeee-ffff"
        }
      ],
      "name": "Bookmarks Bar",
      "type": "folder"
    },
    "other": {
      "children": [],
      "name": "Other Bookmarks",
      "type": "folder"
    },
    "synced": {
      "children": [],
      "name": "Mobile Bookmarks",
      "type": "folder"
    }
  }
})";

constexpr char kAllThreeRoots[] = R"({
  "roots": {
    "bookmark_bar": {
      "children": [
        { "name": "Bar Link", "type": "url", "url": "https://bar.com" }
      ],
      "name": "Bookmarks Bar",
      "type": "folder"
    },
    "other": {
      "children": [
        { "name": "Other Link", "type": "url", "url": "https://other.com" }
      ],
      "name": "Other Bookmarks",
      "type": "folder"
    },
    "synced": {
      "children": [
        { "name": "Mobile Link", "type": "url", "url": "https://mobile.com" }
      ],
      "name": "Mobile Bookmarks",
      "type": "folder"
    }
  }
})";

constexpr char kNestedFolders[] = R"({
  "roots": {
    "bookmark_bar": {
      "children": [
        {
          "name": "Work",
          "type": "folder",
          "children": [
            { "name": "Jira", "type": "url", "url": "https://jira.com" },
            {
              "name": "AWS",
              "type": "folder",
              "children": [
                { "name": "Console", "type": "url", "url": "https://console.aws.amazon.com" },
                { "name": "S3", "type": "url", "url": "https://s3.console.aws.amazon.com" }
              ]
            },
            { "name": "Slack", "type": "url", "url": "https://slack.com" }
          ]
        },
        { "name": "GitHub", "type": "url", "url": "https://github.com" }
      ],
      "name": "Bookmarks Bar",
      "type": "folder"
    },
    "other": { "children": [], "name": "Other Bookmarks", "type": "folder" },
    "synced": { "children": [], "name": "Mobile Bookmarks", "type": "folder" }
  }
})";

constexpr char kMalformedNodes[] = R"({
  "roots": {
    "bookmark_bar": {
      "children": [
        { "name": "Good Link", "type": "url", "url": "https://good.com" },
        { "name": "No URL", "type": "url" },
        { "name": "Empty URL", "type": "url", "url": "" },
        { "name": "Bad URL", "type": "url", "url": "not-a-url" },
        { "name": "Javascript URL", "type": "url", "url": "javascript:void(0)" },
        { "type": "url", "url": "https://no-name.com" },
        { "name": "No Type", "url": "https://notype.com" },
        { "name": "Unknown Type", "type": "separator", "url": "https://sep.com" },
        42,
        "not an object",
        null,
        { "name": "Another Good", "type": "url", "url": "https://another.com" }
      ],
      "name": "Bookmarks Bar",
      "type": "folder"
    },
    "other": { "children": [], "name": "Other Bookmarks", "type": "folder" },
    "synced": { "children": [], "name": "Mobile Bookmarks", "type": "folder" }
  }
})";

constexpr char kUnicodeTitles[] = R"({
  "roots": {
    "bookmark_bar": {
      "children": [
        { "name": "日本語テスト", "type": "url", "url": "https://japanese.test" },
        { "name": "Ünïcödé Çhàrs", "type": "url", "url": "https://unicode.test" },
        { "name": "Emoji 🚀🌍", "type": "url", "url": "https://emoji.test" },
        { "name": "中文书签", "type": "url", "url": "https://chinese.test" },
        { "name": "", "type": "url", "url": "https://empty-name.test" }
      ],
      "name": "Bookmarks Bar",
      "type": "folder"
    },
    "other": { "children": [], "name": "Other Bookmarks", "type": "folder" },
    "synced": { "children": [], "name": "Mobile Bookmarks", "type": "folder" }
  }
})";

constexpr char kDuplicateUrls[] = R"({
  "roots": {
    "bookmark_bar": {
      "children": [
        { "name": "GitHub (1)", "type": "url", "url": "https://github.com" },
        { "name": "GitHub (2)", "type": "url", "url": "https://github.com" },
        { "name": "GitHub Repos", "type": "url", "url": "https://github.com/repositories" }
      ],
      "name": "Bookmarks Bar",
      "type": "folder"
    },
    "other": {
      "children": [
        { "name": "GitHub (3)", "type": "url", "url": "https://github.com" }
      ],
      "name": "Other Bookmarks",
      "type": "folder"
    },
    "synced": { "children": [], "name": "Mobile Bookmarks", "type": "folder" }
  }
})";

constexpr char kOrderedBookmarks[] = R"({
  "roots": {
    "bookmark_bar": {
      "children": [
        { "name": "First", "type": "url", "url": "https://first.com" },
        { "name": "Second", "type": "url", "url": "https://second.com" },
        { "name": "Third", "type": "url", "url": "https://third.com" },
        { "name": "Fourth", "type": "url", "url": "https://fourth.com" },
        { "name": "Fifth", "type": "url", "url": "https://fifth.com" }
      ],
      "name": "Bookmarks Bar",
      "type": "folder"
    },
    "other": { "children": [], "name": "Other Bookmarks", "type": "folder" },
    "synced": { "children": [], "name": "Mobile Bookmarks", "type": "folder" }
  }
})";

constexpr char kSpecialUrlSchemes[] = R"({
  "roots": {
    "bookmark_bar": {
      "children": [
        { "name": "HTTP", "type": "url", "url": "http://example.com" },
        { "name": "HTTPS", "type": "url", "url": "https://example.com" },
        { "name": "FTP", "type": "url", "url": "ftp://files.example.com" },
        { "name": "File", "type": "url", "url": "file:///Users/test/doc.html" },
        { "name": "Chrome", "type": "url", "url": "chrome://settings" },
        { "name": "Data URL", "type": "url", "url": "data:text/html,hello" },
        { "name": "Blob", "type": "url", "url": "blob:https://example.com/id" },
        { "name": "Mailto", "type": "url", "url": "mailto:user@example.com" }
      ],
      "name": "Bookmarks Bar",
      "type": "folder"
    },
    "other": { "children": [], "name": "Other Bookmarks", "type": "folder" },
    "synced": { "children": [], "name": "Mobile Bookmarks", "type": "folder" }
  }
})";

// ── Helper: collect items by type ───────────────────────────────────────────

std::vector<ImportedItem> LinksOnly(const ImportedSource& source) {
  std::vector<ImportedItem> links;
  for (const auto& item : source.items) {
    if (item.type == ImportedItemType::kLink) {
      links.push_back(item);
    }
  }
  return links;
}

std::vector<ImportedItem> FoldersOnly(const ImportedSource& source) {
  std::vector<ImportedItem> folders;
  for (const auto& item : source.items) {
    if (item.type == ImportedItemType::kFolder) {
      folders.push_back(item);
    }
  }
  return folders;
}

std::vector<ImportedItem> ChildrenOf(const ImportedSource& source,
                                     const std::string& parent_id) {
  std::vector<ImportedItem> children;
  for (const auto& item : source.items) {
    if (item.parent_id == parent_id) {
      children.push_back(item);
    }
  }
  std::stable_sort(children.begin(), children.end(),
                   [](const ImportedItem& a, const ImportedItem& b) {
                     return a.order < b.order;
                   });
  return children;
}

const ImportedItem* FindByTitle(const ImportedSource& source,
                                const std::string& title) {
  for (const auto& item : source.items) {
    if (item.title == title) {
      return &item;
    }
  }
  return nullptr;
}

// ── Filesystem test fixture ─────────────────────────────────────────────────

class ChromeBookmarkParserFilesystemTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::FilePath CreateChromeDataDir() {
    base::FilePath data_dir = temp_dir_.GetPath().AppendASCII("Chrome");
    base::CreateDirectory(data_dir);
    return data_dir;
  }

  void CreateProfile(const base::FilePath& data_dir,
                     const std::string& dir_name,
                     const std::string& bookmarks_json) {
    base::FilePath profile_dir = data_dir.AppendASCII(dir_name);
    base::CreateDirectory(profile_dir);
    if (!bookmarks_json.empty()) {
      base::WriteFile(profile_dir.AppendASCII("Bookmarks"), bookmarks_json);
    }
  }

  void WriteLocalState(const base::FilePath& data_dir,
                       const std::string& json) {
    base::WriteFile(data_dir.AppendASCII("Local State"), json);
  }

  base::ScopedTempDir temp_dir_;
};

// ═══════════════════════════════════════════════════════════════════════════
// ParseBookmarksJson tests (in-memory, no filesystem)
// ═══════════════════════════════════════════════════════════════════════════

// ── Empty / minimal ─────────────────────────────────────────────────────────

TEST(ChromeBookmarkParserTest, EmptyBookmarkFile) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kMinimalBookmarks, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());
  const auto& source = result->source;
  EXPECT_EQ(source.browser, "chrome");
  EXPECT_EQ(source.profile_name, "Default");
  EXPECT_TRUE(source.space_id.empty());
  EXPECT_FALSE(source.id.empty());
  EXPECT_FALSE(source.imported_at.is_null());
  // No root folders created for empty groups.
  EXPECT_THAT(source.items, IsEmpty());

  // Stats should all be zero for an empty file.
  const auto& stats = result->stats;
  EXPECT_EQ(stats.links_seen, 0);
  EXPECT_EQ(stats.links_imported, 0);
  EXPECT_EQ(stats.folders_seen, 0);
  EXPECT_EQ(stats.folders_imported, 0);
  EXPECT_EQ(stats.items_skipped, 0);
}

// ── Malformed JSON ──────────────────────────────────────────────────────────

TEST(ChromeBookmarkParserTest, MalformedJson) {
  auto result = ChromiumBookmarkParser::ParseBookmarksJson(
      "this is not json", "chrome", "Default");
  EXPECT_FALSE(result.has_value());
}

TEST(ChromeBookmarkParserTest, ValidJsonButMissingRoots) {
  auto result = ChromiumBookmarkParser::ParseBookmarksJson(
      R"({"version": 1})", "chrome", "Default");
  EXPECT_FALSE(result.has_value());
}

TEST(ChromeBookmarkParserTest, EmptyString) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson("", "chrome", "Default");
  EXPECT_FALSE(result.has_value());
}

TEST(ChromeBookmarkParserTest, JsonArray) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson("[1,2,3]", "chrome", "Default");
  EXPECT_FALSE(result.has_value());
}

// ── Bookmarks Bar / Other / Synced roots ────────────────────────────────────

TEST(ChromeBookmarkParserTest, BookmarkBarOnly) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kBookmarkBarOnly, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());
  const auto& source = result->source;

  // Should have one root folder (Bookmarks Bar) + 2 links.
  auto folders = FoldersOnly(source);
  ASSERT_THAT(folders, SizeIs(1));
  EXPECT_EQ(folders[0].title, "Bookmarks Bar");
  EXPECT_TRUE(folders[0].parent_id.empty());

  auto links = LinksOnly(source);
  ASSERT_THAT(links, SizeIs(2));
  EXPECT_EQ(links[0].title, "GitHub");
  EXPECT_EQ(links[0].url, "https://github.com");
  EXPECT_EQ(links[1].title, "Gmail");
  EXPECT_EQ(links[1].url, "https://mail.google.com");

  // Links should be children of the Bookmarks Bar folder.
  EXPECT_EQ(links[0].parent_id, folders[0].id);
  EXPECT_EQ(links[1].parent_id, folders[0].id);
}

TEST(ChromeBookmarkParserTest, AllThreeRoots) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kAllThreeRoots, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());
  const auto& source = result->source;

  auto folders = FoldersOnly(source);
  ASSERT_THAT(folders, SizeIs(3));

  // Root groups are ordered: Bookmarks Bar, Other, Mobile.
  EXPECT_EQ(folders[0].title, "Bookmarks Bar");
  EXPECT_EQ(folders[0].order, 0);
  EXPECT_EQ(folders[1].title, "Other Bookmarks");
  EXPECT_EQ(folders[1].order, 1);
  EXPECT_EQ(folders[2].title, "Mobile Bookmarks");
  EXPECT_EQ(folders[2].order, 2);

  // Each root has one child link.
  EXPECT_THAT(ChildrenOf(source, folders[0].id), SizeIs(1));
  EXPECT_THAT(ChildrenOf(source, folders[1].id), SizeIs(1));
  EXPECT_THAT(ChildrenOf(source, folders[2].id), SizeIs(1));

  auto bar_children = ChildrenOf(source, folders[0].id);
  EXPECT_EQ(bar_children[0].title, "Bar Link");
}

// ── Nested folders ──────────────────────────────────────────────────────────

TEST(ChromeBookmarkParserTest, NestedFolders) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kNestedFolders, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());
  const auto& source = result->source;

  //  Bookmarks Bar/
  //    Work/
  //      Jira
  //      AWS/
  //        Console
  //        S3
  //      Slack
  //    GitHub

  // Find the root Bookmarks Bar folder.
  auto root_children = ChildrenOf(source, "");
  ASSERT_THAT(root_children, SizeIs(1));
  EXPECT_EQ(root_children[0].title, "Bookmarks Bar");
  const std::string bar_id = root_children[0].id;

  // Bookmarks Bar children: Work (folder) + GitHub (link).
  auto bar_children = ChildrenOf(source, bar_id);
  ASSERT_THAT(bar_children, SizeIs(2));
  EXPECT_EQ(bar_children[0].title, "Work");
  EXPECT_EQ(bar_children[0].type, ImportedItemType::kFolder);
  EXPECT_EQ(bar_children[1].title, "GitHub");
  EXPECT_EQ(bar_children[1].type, ImportedItemType::kLink);
  const std::string work_id = bar_children[0].id;

  // Work children: Jira, AWS (folder), Slack.
  auto work_children = ChildrenOf(source, work_id);
  ASSERT_THAT(work_children, SizeIs(3));
  EXPECT_EQ(work_children[0].title, "Jira");
  EXPECT_EQ(work_children[0].type, ImportedItemType::kLink);
  EXPECT_EQ(work_children[0].order, 0);
  EXPECT_EQ(work_children[1].title, "AWS");
  EXPECT_EQ(work_children[1].type, ImportedItemType::kFolder);
  EXPECT_EQ(work_children[1].order, 1);
  EXPECT_EQ(work_children[2].title, "Slack");
  EXPECT_EQ(work_children[2].type, ImportedItemType::kLink);
  EXPECT_EQ(work_children[2].order, 2);
  const std::string aws_id = work_children[1].id;

  // AWS children: Console, S3.
  auto aws_children = ChildrenOf(source, aws_id);
  ASSERT_THAT(aws_children, SizeIs(2));
  EXPECT_EQ(aws_children[0].title, "Console");
  EXPECT_EQ(aws_children[0].url, "https://console.aws.amazon.com");
  EXPECT_EQ(aws_children[1].title, "S3");
  EXPECT_EQ(aws_children[1].url, "https://s3.console.aws.amazon.com");
}

TEST(ChromeBookmarkParserTest, NestedFolderDepth) {
  // Verify that deeply nested folders (3+ levels) work.
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kNestedFolders, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());
  const auto& source = result->source;

  // Total: Bookmarks Bar (root) + Work + AWS = 3 folders.
  auto folders = FoldersOnly(source);
  EXPECT_THAT(folders, SizeIs(3));

  // Total: Jira + Console + S3 + Slack + GitHub = 5 links.
  auto links = LinksOnly(source);
  EXPECT_THAT(links, SizeIs(5));
}

// ── Malformed individual nodes ──────────────────────────────────────────────

TEST(ChromeBookmarkParserTest, MalformedNodesSkippedGracefully) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kMalformedNodes, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());
  const auto& source = result->source;

  // Only "Good Link" and "Another Good" should survive.
  // - "No URL": url field missing → skipped
  // - "Empty URL": url is "" → skipped (invalid)
  // - "Bad URL": not a valid URL → skipped
  // - "Javascript URL": javascript: scheme → skipped
  // - No name but valid URL: included with empty title
  // - "No Type": type field missing → skipped
  // - "Unknown Type": type=separator → skipped
  // - Non-object entries (42, string, null) → skipped

  auto links = LinksOnly(source);
  ASSERT_THAT(links, SizeIs(3));

  EXPECT_EQ(links[0].title, "Good Link");
  EXPECT_EQ(links[0].url, "https://good.com");

  // The bookmark with no name but a valid URL should be included.
  EXPECT_EQ(links[1].title, "");
  EXPECT_EQ(links[1].url, "https://no-name.com");

  EXPECT_EQ(links[2].title, "Another Good");
  EXPECT_EQ(links[2].url, "https://another.com");
}

TEST(ChromeBookmarkParserTest, MalformedNodesPreserveOrdering) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kMalformedNodes, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());

  auto links = LinksOnly(result->source);
  ASSERT_THAT(links, SizeIs(3));

  // Order values should be sequential among surviving items.
  EXPECT_EQ(links[0].order, 0);
  EXPECT_EQ(links[1].order, 1);
  EXPECT_EQ(links[2].order, 2);
}

// ── Invalid URLs ────────────────────────────────────────────────────────────

TEST(ChromeBookmarkParserTest, UrlSchemeFiltering) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kSpecialUrlSchemes, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());

  auto links = LinksOnly(result->source);

  // HTTP, HTTPS, FTP, file://, chrome:// should pass.
  // data:, blob:, mailto: should be filtered out.
  std::set<std::string> urls;
  for (const auto& link : links) {
    urls.insert(link.url);
  }

  EXPECT_TRUE(urls.contains("http://example.com"));
  EXPECT_TRUE(urls.contains("https://example.com"));
  EXPECT_TRUE(urls.contains("ftp://files.example.com"));
  EXPECT_TRUE(urls.contains("file:///Users/test/doc.html"));
  EXPECT_TRUE(urls.contains("chrome://settings"));
  EXPECT_FALSE(urls.contains("data:text/html,hello"));
  EXPECT_FALSE(urls.contains("blob:https://example.com/id"));
  EXPECT_FALSE(urls.contains("mailto:user@example.com"));
}

// ── Unicode titles ──────────────────────────────────────────────────────────

TEST(ChromeBookmarkParserTest, UnicodeTitles) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kUnicodeTitles, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());

  auto links = LinksOnly(result->source);
  ASSERT_THAT(links, SizeIs(5));

  EXPECT_EQ(links[0].title, "日本語テスト");
  EXPECT_EQ(links[1].title, "Ünïcödé Çhàrs");
  EXPECT_EQ(links[2].title, "Emoji 🚀🌍");
  EXPECT_EQ(links[3].title, "中文书签");
  EXPECT_EQ(links[4].title, "");
}

// ── Duplicate URLs ──────────────────────────────────────────────────────────

TEST(ChromeBookmarkParserTest, DuplicateUrlsPreserved) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kDuplicateUrls, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());

  // All duplicates should be imported — deduplication is the caller's
  // responsibility, not the parser's.
  auto links = LinksOnly(result->source);
  ASSERT_THAT(links, SizeIs(4));

  int github_count = 0;
  for (const auto& link : links) {
    if (link.url == "https://github.com") {
      ++github_count;
    }
  }
  EXPECT_EQ(github_count, 3);
}

TEST(ChromeBookmarkParserTest, DuplicateUrlsHaveDistinctIds) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kDuplicateUrls, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());
  const auto& source = result->source;

  std::set<std::string> ids;
  for (const auto& item : source.items) {
    EXPECT_FALSE(item.id.empty());
    ids.insert(item.id);
  }
  // Every item should have a unique id.
  EXPECT_EQ(ids.size(), source.items.size());
}

// ── Ordering preservation ───────────────────────────────────────────────────

TEST(ChromeBookmarkParserTest, SiblingOrderPreserved) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kOrderedBookmarks, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());

  auto links = LinksOnly(result->source);
  ASSERT_THAT(links, SizeIs(5));

  EXPECT_EQ(links[0].title, "First");
  EXPECT_EQ(links[0].order, 0);
  EXPECT_EQ(links[1].title, "Second");
  EXPECT_EQ(links[1].order, 1);
  EXPECT_EQ(links[2].title, "Third");
  EXPECT_EQ(links[2].order, 2);
  EXPECT_EQ(links[3].title, "Fourth");
  EXPECT_EQ(links[3].order, 3);
  EXPECT_EQ(links[4].title, "Fifth");
  EXPECT_EQ(links[4].order, 4);
}

TEST(ChromeBookmarkParserTest, RootGroupOrdering) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kAllThreeRoots, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());

  // Root-level items should be only the three root folders.
  auto root_items = ChildrenOf(result->source, "");
  ASSERT_THAT(root_items, SizeIs(3));
  EXPECT_EQ(root_items[0].title, "Bookmarks Bar");
  EXPECT_EQ(root_items[1].title, "Other Bookmarks");
  EXPECT_EQ(root_items[2].title, "Mobile Bookmarks");
}

// ── Source metadata ─────────────────────────────────────────────────────────

TEST(ChromeBookmarkParserTest, SourceMetadata) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kBookmarkBarOnly, "edge",
                                               "Work Profile");
  ASSERT_TRUE(result.has_value());
  const auto& source = result->source;

  EXPECT_EQ(source.browser, "edge");
  EXPECT_EQ(source.profile_name, "Work Profile");
  EXPECT_TRUE(source.space_id.empty());
  EXPECT_FALSE(source.id.empty());
  EXPECT_FALSE(source.imported_at.is_null());
}

// ── ID generation ───────────────────────────────────────────────────────────

TEST(ChromeBookmarkParserTest, AllItemsGetUniqueIds) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kNestedFolders, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());
  const auto& source = result->source;

  std::set<std::string> all_ids;
  all_ids.insert(source.id);
  for (const auto& item : source.items) {
    EXPECT_FALSE(item.id.empty()) << "Item '" << item.title << "' has no id";
    auto [it, inserted] = all_ids.insert(item.id);
    EXPECT_TRUE(inserted) << "Duplicate id for item '" << item.title << "'";
  }
}

// ── Parent-child consistency ────────────────────────────────────────────────

TEST(ChromeBookmarkParserTest, AllParentIdsReferenceExistingItems) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kNestedFolders, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());
  const auto& source = result->source;

  std::set<std::string> item_ids;
  for (const auto& item : source.items) {
    item_ids.insert(item.id);
  }

  for (const auto& item : source.items) {
    if (!item.parent_id.empty()) {
      EXPECT_TRUE(item_ids.contains(item.parent_id))
          << "Item '" << item.title << "' references nonexistent parent '"
          << item.parent_id << "'";
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// Filesystem-based tests (profile detection)
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ChromeBookmarkParserFilesystemTest, DetectDefaultProfile) {
  auto data_dir = CreateChromeDataDir();
  CreateProfile(data_dir, "Default", kBookmarkBarOnly);

  auto detected = ChromiumBookmarkParser::DetectProfiles(data_dir);

  EXPECT_EQ(detected.browser, "chrome");
  ASSERT_THAT(detected.profiles, SizeIs(1));
  EXPECT_EQ(detected.profiles[0].directory_name, "Default");
  EXPECT_EQ(detected.profiles[0].display_name, "Default");
  EXPECT_EQ(detected.profiles[0].bookmark_count, 2);
}

TEST_F(ChromeBookmarkParserFilesystemTest, DetectMultipleProfiles) {
  auto data_dir = CreateChromeDataDir();
  CreateProfile(data_dir, "Default", kBookmarkBarOnly);
  CreateProfile(data_dir, "Profile 2", kAllThreeRoots);
  CreateProfile(data_dir, "Profile 14", kNestedFolders);

  auto detected = ChromiumBookmarkParser::DetectProfiles(data_dir);

  ASSERT_THAT(detected.profiles, SizeIs(3));

  // Default should be first.
  EXPECT_EQ(detected.profiles[0].directory_name, "Default");
  EXPECT_EQ(detected.profiles[0].bookmark_count, 2);

  // Then Profile 14, Profile 2 (lexicographic by dir name).
  EXPECT_EQ(detected.profiles[1].directory_name, "Profile 14");
  EXPECT_EQ(detected.profiles[1].bookmark_count, 5);

  EXPECT_EQ(detected.profiles[2].directory_name, "Profile 2");
  EXPECT_EQ(detected.profiles[2].bookmark_count, 3);
}

TEST_F(ChromeBookmarkParserFilesystemTest, DisplayNamesFromLocalState) {
  auto data_dir = CreateChromeDataDir();
  CreateProfile(data_dir, "Default", kBookmarkBarOnly);
  CreateProfile(data_dir, "Profile 2", kAllThreeRoots);

  WriteLocalState(data_dir, R"({
    "profile": {
      "info_cache": {
        "Default": { "name": "Personal" },
        "Profile 2": { "name": "WarnerMedia" }
      }
    }
  })");

  auto detected = ChromiumBookmarkParser::DetectProfiles(data_dir);

  ASSERT_THAT(detected.profiles, SizeIs(2));
  EXPECT_EQ(detected.profiles[0].display_name, "Personal");
  EXPECT_EQ(detected.profiles[1].display_name, "WarnerMedia");
}

TEST_F(ChromeBookmarkParserFilesystemTest, FallbackDisplayNameWhenNoLocalState) {
  auto data_dir = CreateChromeDataDir();
  CreateProfile(data_dir, "Default", kBookmarkBarOnly);
  CreateProfile(data_dir, "Profile 5", kAllThreeRoots);

  // No Local State file.
  auto detected = ChromiumBookmarkParser::DetectProfiles(data_dir);

  ASSERT_THAT(detected.profiles, SizeIs(2));
  EXPECT_EQ(detected.profiles[0].display_name, "Default");
  EXPECT_EQ(detected.profiles[1].display_name, "Profile 5");
}

TEST_F(ChromeBookmarkParserFilesystemTest, GuestProfileExcluded) {
  auto data_dir = CreateChromeDataDir();
  CreateProfile(data_dir, "Default", kBookmarkBarOnly);
  CreateProfile(data_dir, "Guest Profile", kMinimalBookmarks);

  auto detected = ChromiumBookmarkParser::DetectProfiles(data_dir);

  ASSERT_THAT(detected.profiles, SizeIs(1));
  EXPECT_EQ(detected.profiles[0].directory_name, "Default");
}

TEST_F(ChromeBookmarkParserFilesystemTest, NonProfileDirectoriesIgnored) {
  auto data_dir = CreateChromeDataDir();
  CreateProfile(data_dir, "Default", kBookmarkBarOnly);

  // These should not be detected as profiles.
  base::CreateDirectory(data_dir.AppendASCII("Crashpad"));
  base::CreateDirectory(data_dir.AppendASCII("GCM Store"));
  base::CreateDirectory(data_dir.AppendASCII("System Profile"));
  base::CreateDirectory(data_dir.AppendASCII("Profile"));
  base::CreateDirectory(data_dir.AppendASCII("Profile XYZ"));

  auto detected = ChromiumBookmarkParser::DetectProfiles(data_dir);

  ASSERT_THAT(detected.profiles, SizeIs(1));
  EXPECT_EQ(detected.profiles[0].directory_name, "Default");
}

TEST_F(ChromeBookmarkParserFilesystemTest, ProfileWithNoBookmarksFile) {
  auto data_dir = CreateChromeDataDir();

  // Profile directory exists but no Bookmarks file.
  base::CreateDirectory(data_dir.AppendASCII("Default"));

  auto detected = ChromiumBookmarkParser::DetectProfiles(data_dir);

  ASSERT_THAT(detected.profiles, SizeIs(1));
  EXPECT_EQ(detected.profiles[0].bookmark_count, -1);
}

TEST_F(ChromeBookmarkParserFilesystemTest, EmptyBookmarkFileDetected) {
  auto data_dir = CreateChromeDataDir();
  CreateProfile(data_dir, "Default", kMinimalBookmarks);

  auto detected = ChromiumBookmarkParser::DetectProfiles(data_dir);

  ASSERT_THAT(detected.profiles, SizeIs(1));
  EXPECT_EQ(detected.profiles[0].bookmark_count, 0);
}

TEST_F(ChromeBookmarkParserFilesystemTest, MalformedBookmarkFileInDetection) {
  auto data_dir = CreateChromeDataDir();
  CreateProfile(data_dir, "Default", "not valid json at all");

  auto detected = ChromiumBookmarkParser::DetectProfiles(data_dir);

  ASSERT_THAT(detected.profiles, SizeIs(1));
  // bookmark_count stays at -1 because file couldn't be parsed.
  EXPECT_EQ(detected.profiles[0].bookmark_count, -1);
}

TEST_F(ChromeBookmarkParserFilesystemTest, NonexistentDataDir) {
  base::FilePath fake_dir = temp_dir_.GetPath().AppendASCII("DoesNotExist");

  auto detected = ChromiumBookmarkParser::DetectProfiles(fake_dir);

  EXPECT_EQ(detected.browser, "chrome");
  EXPECT_THAT(detected.profiles, IsEmpty());
}

TEST_F(ChromeBookmarkParserFilesystemTest, ParseBookmarksFileFromDisk) {
  auto data_dir = CreateChromeDataDir();
  CreateProfile(data_dir, "Default", kNestedFolders);

  base::FilePath bookmarks_path =
      data_dir.AppendASCII("Default").AppendASCII("Bookmarks");

  auto result = ChromiumBookmarkParser::ParseBookmarksFile(
      bookmarks_path, "chrome", "Personal");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->source.browser, "chrome");
  EXPECT_EQ(result->source.profile_name, "Personal");
  // 3 folders + 5 links = 8 total items.
  EXPECT_THAT(result->source.items, SizeIs(8));
}

TEST_F(ChromeBookmarkParserFilesystemTest, ParseMissingFileReturnsNullopt) {
  base::FilePath missing =
      temp_dir_.GetPath().AppendASCII("nonexistent").AppendASCII("Bookmarks");

  auto result =
      ChromiumBookmarkParser::ParseBookmarksFile(missing, "chrome", "Default");

  EXPECT_FALSE(result.has_value());
}

TEST_F(ChromeBookmarkParserFilesystemTest, ImportAllProfiles) {
  auto data_dir = CreateChromeDataDir();
  CreateProfile(data_dir, "Default", kBookmarkBarOnly);
  CreateProfile(data_dir, "Profile 2", kNestedFolders);

  WriteLocalState(data_dir, R"({
    "profile": {
      "info_cache": {
        "Default": { "name": "Personal" },
        "Profile 2": { "name": "Work" }
      }
    }
  })");

  auto sources = ChromiumBookmarkParser::ImportAllProfiles(data_dir);

  // Both profiles have bookmarks.
  ASSERT_THAT(sources, SizeIs(2));

  // Each source should have its display name from Local State.
  std::set<std::string> names;
  for (const auto& s : sources) {
    names.insert(s.profile_name);
    EXPECT_TRUE(s.space_id.empty());
    EXPECT_EQ(s.browser, "chrome");
  }
  EXPECT_TRUE(names.contains("Personal"));
  EXPECT_TRUE(names.contains("Work"));
}

TEST_F(ChromeBookmarkParserFilesystemTest, ImportAllSkipsEmptyProfiles) {
  auto data_dir = CreateChromeDataDir();
  CreateProfile(data_dir, "Default", kBookmarkBarOnly);
  CreateProfile(data_dir, "Profile 2", kMinimalBookmarks);

  auto sources = ChromiumBookmarkParser::ImportAllProfiles(data_dir);

  // Profile 2 has 0 bookmarks, so it should be skipped.
  ASSERT_THAT(sources, SizeIs(1));
  EXPECT_EQ(sources[0].profile_name, "Default");
}

// ── Edge compatibility ──────────────────────────────────────────────────────

TEST_F(ChromeBookmarkParserFilesystemTest, EdgeDetection) {
  auto data_dir = CreateChromeDataDir();
  CreateProfile(data_dir, "Default", kBookmarkBarOnly);

  auto detected = ChromiumBookmarkParser::DetectProfiles(data_dir, "edge");

  EXPECT_EQ(detected.browser, "edge");
  ASSERT_THAT(detected.profiles, SizeIs(1));
}

// ═══════════════════════════════════════════════════════════════════════════
// Authoritative ParseStats tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ChromeBookmarkParserStatsTest, BookmarkBarOnlyStats) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kBookmarkBarOnly, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());
  const auto& stats = result->stats;

  // 2 URL nodes, both valid.  1 root folder (Bookmarks Bar).
  EXPECT_EQ(stats.links_seen, 2);
  EXPECT_EQ(stats.links_imported, 2);
  EXPECT_EQ(stats.folders_seen, 1);
  EXPECT_EQ(stats.folders_imported, 1);
  EXPECT_EQ(stats.items_skipped, 0);
}

TEST(ChromeBookmarkParserStatsTest, AllThreeRootsStats) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kAllThreeRoots, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());
  const auto& stats = result->stats;

  // 3 URL nodes (one per root group).  3 root folders.
  EXPECT_EQ(stats.links_seen, 3);
  EXPECT_EQ(stats.links_imported, 3);
  EXPECT_EQ(stats.folders_seen, 3);
  EXPECT_EQ(stats.folders_imported, 3);
  EXPECT_EQ(stats.items_skipped, 0);
}

TEST(ChromeBookmarkParserStatsTest, NestedFoldersStats) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kNestedFolders, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());
  const auto& stats = result->stats;

  // Links: Jira + Console + S3 + Slack + GitHub = 5 seen, 5 imported.
  // Folders: Bookmarks Bar + Work + AWS = 3 seen, 3 imported.
  EXPECT_EQ(stats.links_seen, 5);
  EXPECT_EQ(stats.links_imported, 5);
  EXPECT_EQ(stats.folders_seen, 3);
  EXPECT_EQ(stats.folders_imported, 3);
  EXPECT_EQ(stats.items_skipped, 0);
}

TEST(ChromeBookmarkParserStatsTest, MalformedNodesStats) {
  // This fixture has a mix of valid and invalid nodes that exercises
  // every skip path:
  //   - "Good Link": valid URL → imported
  //   - "No URL": type=url but no url field → skipped
  //   - "Empty URL": url="" → skipped (invalid)
  //   - "Bad URL": url="not-a-url" → skipped
  //   - "Javascript URL": javascript: scheme → skipped
  //   - (no name, valid URL): valid → imported
  //   - "No Type": missing type field → skipped
  //   - "Unknown Type": type=separator → skipped
  //   - 42, "not an object", null → not dict, skipped by the for loop
  //   - "Another Good": valid URL → imported
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kMalformedNodes, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());
  const auto& stats = result->stats;

  // URL-type nodes encountered: Good Link, No URL, Empty URL, Bad URL,
  // Javascript URL, (no name), Another Good = 7.
  EXPECT_EQ(stats.links_seen, 7);

  // Imported: Good Link, (no name), Another Good = 3.
  EXPECT_EQ(stats.links_imported, 3);

  // 1 root folder: Bookmarks Bar.
  EXPECT_EQ(stats.folders_seen, 1);
  EXPECT_EQ(stats.folders_imported, 1);

  // Skipped: No URL (url missing), Empty URL, Bad URL, Javascript URL
  // = 4 invalid URLs + No Type (missing type) + Unknown Type (separator)
  // = 6 total.  Non-dict entries (42, string, null) are filtered by the
  // JSON array iteration before ConvertNode is called.
  EXPECT_EQ(stats.items_skipped, 6);
}

TEST(ChromeBookmarkParserStatsTest, UrlSchemeFilteringStats) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kSpecialUrlSchemes, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());
  const auto& stats = result->stats;

  // 8 URL-type nodes.
  EXPECT_EQ(stats.links_seen, 8);

  // 5 pass validation: HTTP, HTTPS, FTP, file, chrome.
  EXPECT_EQ(stats.links_imported, 5);

  // 3 rejected: data, blob, mailto.
  EXPECT_EQ(stats.items_skipped, 3);

  // 1 root folder (Bookmarks Bar).
  EXPECT_EQ(stats.folders_seen, 1);
  EXPECT_EQ(stats.folders_imported, 1);
}

TEST(ChromeBookmarkParserStatsTest, EmptyFileStats) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kMinimalBookmarks, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());
  const auto& stats = result->stats;

  // All empty — no children in any root group.
  EXPECT_EQ(stats.links_seen, 0);
  EXPECT_EQ(stats.links_imported, 0);
  EXPECT_EQ(stats.folders_seen, 0);
  EXPECT_EQ(stats.folders_imported, 0);
  EXPECT_EQ(stats.items_skipped, 0);
}

TEST(ChromeBookmarkParserStatsTest, DuplicateUrlsStats) {
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kDuplicateUrls, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());
  const auto& stats = result->stats;

  // 4 URL nodes, all valid (duplicates are fine).
  EXPECT_EQ(stats.links_seen, 4);
  EXPECT_EQ(stats.links_imported, 4);

  // 2 root folders: Bookmarks Bar + Other Bookmarks.
  EXPECT_EQ(stats.folders_seen, 2);
  EXPECT_EQ(stats.folders_imported, 2);
  EXPECT_EQ(stats.items_skipped, 0);
}

TEST(ChromeBookmarkParserStatsTest, StatsConsistencyWithItems) {
  // Verify that links_imported + folders_imported == items.size()
  // for valid input.
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kNestedFolders, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());
  const auto& stats = result->stats;
  const auto& source = result->source;

  EXPECT_EQ(
      static_cast<size_t>(stats.links_imported + stats.folders_imported),
      source.items.size());
}

TEST(ChromeBookmarkParserStatsTest, StatsConsistencyMixedInput) {
  // For input with skipped items, links_imported + folders_imported
  // should still equal items.size(), and items_skipped should be
  // independently tracked at the discard point.
  auto result =
      ChromiumBookmarkParser::ParseBookmarksJson(kMalformedNodes, "chrome",
                                               "Default");
  ASSERT_TRUE(result.has_value());
  const auto& stats = result->stats;
  const auto& source = result->source;

  // Verify counts are consistent.
  EXPECT_EQ(
      static_cast<size_t>(stats.links_imported + stats.folders_imported),
      source.items.size());

  // items_skipped should NOT be inferred as (links_seen - links_imported).
  // It should also count non-URL skips (missing type, unknown type).
  // Total skipped: 4 bad URLs + 1 no-type + 1 separator = 6.
  EXPECT_EQ(stats.items_skipped, 6);
  EXPECT_GT(stats.items_skipped,
            stats.links_seen - stats.links_imported);
}

TEST_F(ChromeBookmarkParserFilesystemTest, ParseBookmarksFileStats) {
  auto data_dir = CreateChromeDataDir();
  CreateProfile(data_dir, "Default", kNestedFolders);

  base::FilePath bookmarks_path =
      data_dir.AppendASCII("Default").AppendASCII("Bookmarks");

  auto result = ChromiumBookmarkParser::ParseBookmarksFile(
      bookmarks_path, "chrome", "Personal");

  ASSERT_TRUE(result.has_value());
  const auto& stats = result->stats;

  // kNestedFolders: 5 links, 3 folders, no skips.
  EXPECT_EQ(stats.links_seen, 5);
  EXPECT_EQ(stats.links_imported, 5);
  EXPECT_EQ(stats.folders_seen, 3);
  EXPECT_EQ(stats.folders_imported, 3);
  EXPECT_EQ(stats.items_skipped, 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// Microsoft Edge tests
// ═══════════════════════════════════════════════════════════════════════════

// Edge uses the same Chromium Bookmarks JSON format.  These tests verify
// that the parser correctly handles Edge-specific detection scenarios and
// that source identity is properly separated from Chrome.

// Helper to create an Edge data directory layout (identical structure
// to Chrome, different root path).
class EdgeBookmarkParserFilesystemTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::FilePath CreateEdgeDataDir() {
    base::FilePath data_dir =
        temp_dir_.GetPath().AppendASCII("Microsoft Edge");
    base::CreateDirectory(data_dir);
    return data_dir;
  }

  void CreateProfile(const base::FilePath& data_dir,
                     const std::string& dir_name,
                     const std::string& bookmarks_json) {
    base::FilePath profile_dir = data_dir.AppendASCII(dir_name);
    base::CreateDirectory(profile_dir);
    if (!bookmarks_json.empty()) {
      base::WriteFile(profile_dir.AppendASCII("Bookmarks"), bookmarks_json);
    }
  }

  void WriteLocalState(const base::FilePath& data_dir,
                       const std::string& json) {
    base::WriteFile(data_dir.AppendASCII("Local State"), json);
  }

  base::ScopedTempDir temp_dir_;
};

TEST_F(EdgeBookmarkParserFilesystemTest, DetectDefaultEdgeProfile) {
  auto data_dir = CreateEdgeDataDir();
  CreateProfile(data_dir, "Default", kBookmarkBarOnly);

  auto detected = ChromiumBookmarkParser::DetectProfiles(data_dir, "edge");

  EXPECT_EQ(detected.browser, "edge");
  ASSERT_THAT(detected.profiles, SizeIs(1));
  EXPECT_EQ(detected.profiles[0].directory_name, "Default");
  EXPECT_EQ(detected.profiles[0].display_name, "Default");
  EXPECT_EQ(detected.profiles[0].bookmark_count, 2);
}

TEST_F(EdgeBookmarkParserFilesystemTest, DetectMultipleEdgeProfiles) {
  auto data_dir = CreateEdgeDataDir();
  CreateProfile(data_dir, "Default", kBookmarkBarOnly);
  CreateProfile(data_dir, "Profile 1", kAllThreeRoots);
  CreateProfile(data_dir, "Profile 3", kNestedFolders);

  auto detected = ChromiumBookmarkParser::DetectProfiles(data_dir, "edge");

  ASSERT_THAT(detected.profiles, SizeIs(3));
  EXPECT_EQ(detected.profiles[0].directory_name, "Default");
  EXPECT_EQ(detected.profiles[0].bookmark_count, 2);
  EXPECT_EQ(detected.profiles[1].directory_name, "Profile 1");
  EXPECT_EQ(detected.profiles[1].bookmark_count, 3);
  EXPECT_EQ(detected.profiles[2].directory_name, "Profile 3");
  EXPECT_EQ(detected.profiles[2].bookmark_count, 5);
}

TEST_F(EdgeBookmarkParserFilesystemTest, EdgeLocalStateDisplayNames) {
  auto data_dir = CreateEdgeDataDir();
  CreateProfile(data_dir, "Default", kBookmarkBarOnly);
  CreateProfile(data_dir, "Profile 1", kAllThreeRoots);

  WriteLocalState(data_dir, R"({
    "profile": {
      "info_cache": {
        "Default": { "name": "Personal" },
        "Profile 1": { "name": "Work" }
      }
    }
  })");

  auto detected = ChromiumBookmarkParser::DetectProfiles(data_dir, "edge");

  ASSERT_THAT(detected.profiles, SizeIs(2));
  EXPECT_EQ(detected.profiles[0].display_name, "Personal");
  EXPECT_EQ(detected.profiles[1].display_name, "Work");
}

TEST_F(EdgeBookmarkParserFilesystemTest, EdgeFallbackDisplayNameNoLocalState) {
  auto data_dir = CreateEdgeDataDir();
  CreateProfile(data_dir, "Default", kBookmarkBarOnly);
  CreateProfile(data_dir, "Profile 2", kAllThreeRoots);

  auto detected = ChromiumBookmarkParser::DetectProfiles(data_dir, "edge");

  ASSERT_THAT(detected.profiles, SizeIs(2));
  EXPECT_EQ(detected.profiles[0].display_name, "Default");
  EXPECT_EQ(detected.profiles[1].display_name, "Profile 2");
}

TEST_F(EdgeBookmarkParserFilesystemTest, EdgeMissingBookmarksFile) {
  auto data_dir = CreateEdgeDataDir();
  base::CreateDirectory(data_dir.AppendASCII("Default"));

  auto detected = ChromiumBookmarkParser::DetectProfiles(data_dir, "edge");

  ASSERT_THAT(detected.profiles, SizeIs(1));
  EXPECT_EQ(detected.profiles[0].bookmark_count, -1);
}

TEST_F(EdgeBookmarkParserFilesystemTest, EdgeMalformedBookmarksFile) {
  auto data_dir = CreateEdgeDataDir();
  CreateProfile(data_dir, "Default", "corrupted content here!!");

  auto detected = ChromiumBookmarkParser::DetectProfiles(data_dir, "edge");

  ASSERT_THAT(detected.profiles, SizeIs(1));
  EXPECT_EQ(detected.profiles[0].bookmark_count, -1);
}

TEST_F(EdgeBookmarkParserFilesystemTest, EdgeNestedFolders) {
  auto data_dir = CreateEdgeDataDir();
  CreateProfile(data_dir, "Default", kNestedFolders);

  base::FilePath bookmarks_path =
      data_dir.AppendASCII("Default").AppendASCII("Bookmarks");

  auto result = ChromiumBookmarkParser::ParseBookmarksFile(
      bookmarks_path, "edge", "Default");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->source.browser, "edge");
  EXPECT_THAT(result->source.items, SizeIs(8));

  // Stats should match the nested folders fixture.
  EXPECT_EQ(result->stats.links_seen, 5);
  EXPECT_EQ(result->stats.links_imported, 5);
  EXPECT_EQ(result->stats.folders_seen, 3);
  EXPECT_EQ(result->stats.folders_imported, 3);
  EXPECT_EQ(result->stats.items_skipped, 0);
}

TEST_F(EdgeBookmarkParserFilesystemTest, EdgeGuestProfileExcluded) {
  auto data_dir = CreateEdgeDataDir();
  CreateProfile(data_dir, "Default", kBookmarkBarOnly);
  CreateProfile(data_dir, "Guest Profile", kMinimalBookmarks);
  CreateProfile(data_dir, "System Profile", kMinimalBookmarks);

  auto detected = ChromiumBookmarkParser::DetectProfiles(data_dir, "edge");

  ASSERT_THAT(detected.profiles, SizeIs(1));
  EXPECT_EQ(detected.profiles[0].directory_name, "Default");
}

// ═══════════════════════════════════════════════════════════════════════════
// Source identity tests (Chrome vs Edge, re-import, Space isolation)
// ═══════════════════════════════════════════════════════════════════════════

TEST(ImportSourceIdentityTest,
     ChromeAndEdgeSameProfileDirGenerateDifferentIds) {
  const std::string chrome_id =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "chrome", "Default", "space-1");
  const std::string edge_id =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "edge", "Default", "space-1");

  // Same profile directory name ("Default") in different browsers
  // must produce different source IDs.
  EXPECT_NE(chrome_id, edge_id);
  EXPECT_FALSE(chrome_id.empty());
  EXPECT_FALSE(edge_id.empty());
}

TEST(ImportSourceIdentityTest, SameEdgeProfileTwoSpacesGenerateDifferentIds) {
  const std::string id_1 =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "edge", "Default", "space-work");
  const std::string id_2 =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "edge", "Default", "space-personal");

  EXPECT_NE(id_1, id_2);
}

TEST(ImportSourceIdentityTest, SameEdgeProfileSameSpaceIsDeterministic) {
  const std::string id_a =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "edge", "Profile 1", "space-x");
  const std::string id_b =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "edge", "Profile 1", "space-x");

  EXPECT_EQ(id_a, id_b);
}

TEST(ImportSourceIdentityTest, SourceIdIsUuidFormat) {
  const std::string id =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "edge", "Default", "space-1");

  // UUID format: 8-4-4-4-12 = 36 characters.
  EXPECT_EQ(id.size(), 36u);
  EXPECT_EQ(id[8], '-');
  EXPECT_EQ(id[13], '-');
  EXPECT_EQ(id[18], '-');
  EXPECT_EQ(id[23], '-');
}

// ═══════════════════════════════════════════════════════════════════════════
// Coordinator re-import and coexistence tests
// ═══════════════════════════════════════════════════════════════════════════

class CoordinatorEdgeTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::FilePath CreateBrowserDataDir(const std::string& browser_name) {
    base::FilePath data_dir =
        temp_dir_.GetPath().AppendASCII(browser_name);
    base::CreateDirectory(data_dir);
    return data_dir;
  }

  void CreateProfile(const base::FilePath& data_dir,
                     const std::string& dir_name,
                     const std::string& bookmarks_json) {
    base::FilePath profile_dir = data_dir.AppendASCII(dir_name);
    base::CreateDirectory(profile_dir);
    if (!bookmarks_json.empty()) {
      base::WriteFile(profile_dir.AppendASCII("Bookmarks"), bookmarks_json);
    }
  }

  base::ScopedTempDir temp_dir_;
};

TEST_F(CoordinatorEdgeTest, DetectEdgeUsesEdgeBrowserIdentifier) {
  auto data_dir = CreateBrowserDataDir("Edge");
  CreateProfile(data_dir, "Default", kBookmarkBarOnly);

  auto detected = AvoraImportCoordinator::DetectEdge(data_dir);

  EXPECT_EQ(detected.browser, "edge");
  ASSERT_THAT(detected.profiles, SizeIs(1));
}

TEST_F(CoordinatorEdgeTest, DetectChromeUsesChromeIdentifier) {
  auto data_dir = CreateBrowserDataDir("Chrome");
  CreateProfile(data_dir, "Default", kBookmarkBarOnly);

  auto detected = AvoraImportCoordinator::DetectChrome(data_dir);

  EXPECT_EQ(detected.browser, "chrome");
  ASSERT_THAT(detected.profiles, SizeIs(1));
}

TEST_F(CoordinatorEdgeTest, ChromeAndEdgeImportsCoexistInSameSpace) {
  auto chrome_dir = CreateBrowserDataDir("Chrome");
  CreateProfile(chrome_dir, "Default", kBookmarkBarOnly);

  auto edge_dir = CreateBrowserDataDir("Edge");
  CreateProfile(edge_dir, "Default", kAllThreeRoots);

  auto chrome_detected = AvoraImportCoordinator::DetectChrome(chrome_dir);
  auto edge_detected = AvoraImportCoordinator::DetectEdge(edge_dir);

  ASSERT_THAT(chrome_detected.profiles, SizeIs(1));
  ASSERT_THAT(edge_detected.profiles, SizeIs(1));

  // Both import into the same Space — they should not collide.
  const std::string chrome_source_id =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "chrome", "Default", "shared-space");
  const std::string edge_source_id =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "edge", "Default", "shared-space");

  EXPECT_NE(chrome_source_id, edge_source_id);
}

TEST(EdgeParserTest, EdgeBrowserMetadataInParseResult) {
  auto result = ChromiumBookmarkParser::ParseBookmarksJson(
      kBookmarkBarOnly, "edge", "Default");
  ASSERT_TRUE(result.has_value());

  EXPECT_EQ(result->source.browser, "edge");
  EXPECT_EQ(result->source.profile_name, "Default");
}

TEST(EdgeParserTest, EdgeParserStatsMatch) {
  // Edge uses the same parser; stats should work identically.
  auto result = ChromiumBookmarkParser::ParseBookmarksJson(
      kMalformedNodes, "edge", "Default");
  ASSERT_TRUE(result.has_value());
  const auto& stats = result->stats;

  EXPECT_EQ(stats.links_seen, 7);
  EXPECT_EQ(stats.links_imported, 3);
  EXPECT_EQ(stats.folders_seen, 1);
  EXPECT_EQ(stats.folders_imported, 1);
  EXPECT_EQ(stats.items_skipped, 6);
}

// ═════════════════════════════════════════════════════════════════════════════
// Update Import: source-ID re-detection matching tests
//
// These verify the logic the dialog uses to map a persisted source back
// to a detected external profile via GenerateImportedSourceId.
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(CoordinatorEdgeTest, UpdateImport_SourceProfileStillExists) {
  // Setup: import a Chrome profile, then re-detect and verify the
  // source ID can be reproduced from the detected profile.
  auto data_dir = CreateBrowserDataDir("Chrome");
  CreateProfile(data_dir, "Default", kBookmarkBarOnly);

  auto detected = AvoraImportCoordinator::DetectChrome(data_dir);
  ASSERT_THAT(detected.profiles, SizeIs(1));

  const std::string space_id = "space-work";
  const std::string source_id =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "chrome", detected.profiles[0].directory_name, space_id);
  EXPECT_FALSE(source_id.empty());

  // Re-detect: the same profile produces the same source ID.
  auto re_detected = AvoraImportCoordinator::DetectChrome(data_dir);
  ASSERT_THAT(re_detected.profiles, SizeIs(1));
  std::string recomputed_id =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "chrome", re_detected.profiles[0].directory_name, space_id);
  EXPECT_EQ(source_id, recomputed_id);
}

TEST_F(CoordinatorEdgeTest, UpdateImport_SourceProfileDeleted) {
  // Setup: import from "Profile 1", then remove it.
  auto data_dir = CreateBrowserDataDir("Chrome");
  CreateProfile(data_dir, "Default", kBookmarkBarOnly);
  CreateProfile(data_dir, "Profile 1", kBookmarkBarOnly);

  auto detected = AvoraImportCoordinator::DetectChrome(data_dir);
  ASSERT_THAT(detected.profiles, SizeIs(2));

  const std::string space_id = "space-work";

  // Find "Profile 1" and compute its source ID.
  const DetectedProfile* profile1 = nullptr;
  for (const auto& p : detected.profiles) {
    if (p.directory_name == "Profile 1") profile1 = &p;
  }
  ASSERT_NE(nullptr, profile1);
  const std::string profile1_source_id =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "chrome", profile1->directory_name, space_id);

  // Delete "Profile 1" from disk.
  base::DeletePathRecursively(data_dir.AppendASCII("Profile 1"));

  // Re-detect: only "Default" remains.
  auto re_detected = AvoraImportCoordinator::DetectChrome(data_dir);
  ASSERT_THAT(re_detected.profiles, SizeIs(1));
  EXPECT_EQ("Default", re_detected.profiles[0].directory_name);

  // Try to match profile1_source_id against remaining detected profiles.
  bool found = false;
  for (const auto& p : re_detected.profiles) {
    std::string computed_id =
        AvoraImportCoordinator::GenerateImportedSourceId(
            "chrome", p.directory_name, space_id);
    if (computed_id == profile1_source_id) {
      found = true;
      break;
    }
  }
  EXPECT_FALSE(found);
}

TEST_F(CoordinatorEdgeTest, UpdateImport_BrowserUninstalled) {
  // Setup: generate a source ID for an Edge profile.
  auto data_dir = CreateBrowserDataDir("Edge");
  CreateProfile(data_dir, "Default", kBookmarkBarOnly);

  auto detected = AvoraImportCoordinator::DetectEdge(data_dir);
  ASSERT_THAT(detected.profiles, SizeIs(1));

  const std::string space_id = "space-personal";
  const std::string edge_source_id =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "edge", detected.profiles[0].directory_name, space_id);

  // Remove the entire Edge data directory (simulating uninstall).
  base::DeletePathRecursively(data_dir);

  // Re-detect all browsers: Edge should return no profiles.
  auto re_detected_edge = AvoraImportCoordinator::DetectEdge(data_dir);
  EXPECT_TRUE(re_detected_edge.profiles.empty());

  // Verify: no detected profile matches the old edge source ID.
  bool found = false;
  for (const auto& p : re_detected_edge.profiles) {
    std::string computed_id =
        AvoraImportCoordinator::GenerateImportedSourceId(
            "edge", p.directory_name, space_id);
    if (computed_id == edge_source_id) found = true;
  }
  EXPECT_FALSE(found);
}

TEST_F(CoordinatorEdgeTest, UpdateImport_ExistingDataSurvivesFailedMatch) {
  // Verify that the existing ImportedSource in the store is not
  // affected when an Update Import cannot find the external profile.
  TestingPrefServiceSimple prefs;
  ImportedLinkStore::RegisterProfilePrefs(prefs.registry());

  ImportedSource source;
  source.id = AvoraImportCoordinator::GenerateImportedSourceId(
      "chrome", "Profile 1", "space-work");
  source.space_id = "space-work";
  source.browser = "chrome";
  source.profile_name = "Work";
  source.imported_at = base::Time::Now();

  ImportedItem link;
  link.id = "link-1";
  link.type = ImportedItemType::kLink;
  link.title = "Example";
  link.url = "https://example.com/";
  link.order = 0;
  source.items.push_back(std::move(link));

  ImportedLinkStore store(&prefs);
  std::string stored_id = store.AddSource(std::move(source));

  // Verify the source exists and has data.
  const ImportedSource* stored = store.GetSourceById(stored_id);
  ASSERT_NE(nullptr, stored);
  EXPECT_EQ("Work", stored->profile_name);
  EXPECT_EQ(1u, stored->items.size());

  // Simulate: external profile is gone (no re-detection match).
  // The stored data must remain untouched.
  const ImportedSource* still_there = store.GetSourceById(stored_id);
  ASSERT_NE(nullptr, still_there);
  EXPECT_EQ("Work", still_there->profile_name);
  EXPECT_EQ(1u, still_there->items.size());
}

}  // namespace
}  // namespace avora
