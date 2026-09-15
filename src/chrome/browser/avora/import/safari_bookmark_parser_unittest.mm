// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/import/safari_bookmark_parser.h"

#include "build/build_config.h"

#if BUILDFLAG(IS_MAC)

#import <Foundation/Foundation.h>

#include <string>
#include <vector>

#include "base/apple/foundation_util.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/avora/avora_imported_links.h"
#include "chrome/browser/avora/import/avora_import_coordinator.h"
#include "chrome/browser/avora/import/bookmark_import_types.h"
#include "chrome/browser/avora/import/chromium_bookmark_parser.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace avora {

namespace {

// Helper: write a plist dictionary to a file as binary plist.
bool WritePlist(NSDictionary* dict, const base::FilePath& path) {
  @autoreleasepool {
    NSURL* url = base::apple::FilePathToNSURL(path);
    NSError* error = nil;
    NSData* data = [NSPropertyListSerialization
        dataWithPropertyList:dict
                      format:NSPropertyListBinaryFormat_v1_0
                     options:0
                       error:&error];
    if (!data) return false;
    return [data writeToURL:url atomically:YES];
  }
}

// Helper: create a minimal valid Safari bookmarks plist.
NSDictionary* MinimalBookmarksPlist() {
  return @{
    @"WebBookmarkFileVersion" : @1,
    @"WebBookmarkType" : @"WebBookmarkTypeList",
    @"Title" : @"",
    @"WebBookmarkUUID" : @"ROOT-UUID",
    @"Children" : @[
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksBar",
        @"WebBookmarkUUID" : @"BAR-UUID",
        @"Children" : @[],
      },
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksMenu",
        @"WebBookmarkUUID" : @"MENU-UUID",
        @"Children" : @[],
      },
    ],
  };
}

// Helper: create a bookmark leaf dictionary.
NSDictionary* BookmarkLeaf(NSString* title, NSString* url) {
  return @{
    @"WebBookmarkType" : @"WebBookmarkTypeLeaf",
    @"WebBookmarkUUID" : [[NSUUID UUID] UUIDString],
    @"URLString" : url,
    @"URIDictionary" : @{@"title" : title},
  };
}

// Helper: create a bookmark folder dictionary.
NSDictionary* BookmarkFolder(NSString* title, NSArray* children) {
  return @{
    @"WebBookmarkType" : @"WebBookmarkTypeList",
    @"WebBookmarkUUID" : [[NSUUID UUID] UUIDString],
    @"Title" : title,
    @"Children" : children,
  };
}

// Helper: create a proxy node (internal Safari node).
NSDictionary* ProxyNode(NSString* title, NSString* identifier) {
  return @{
    @"WebBookmarkType" : @"WebBookmarkTypeProxy",
    @"WebBookmarkUUID" : [[NSUUID UUID] UUIDString],
    @"Title" : title,
    @"WebBookmarkIdentifier" : identifier,
  };
}

// Helper: create a Reading List container with items.
NSDictionary* ReadingListContainer(NSArray* items) {
  return @{
    @"WebBookmarkType" : @"WebBookmarkTypeList",
    @"WebBookmarkUUID" : [[NSUUID UUID] UUIDString],
    @"Title" : @"com.apple.ReadingList",
    @"Children" : items,
  };
}

// Count items of a given type in the source.
int CountItemsByType(const ImportedSource& source, ImportedItemType type) {
  int count = 0;
  for (const auto& item : source.items) {
    if (item.type == type) count++;
  }
  return count;
}

// Find an item by title.
const ImportedItem* FindByTitle(const ImportedSource& source,
                                const std::string& title) {
  for (const auto& item : source.items) {
    if (item.title == title) return &item;
  }
  return nullptr;
}

// Find children of a given parent.
std::vector<const ImportedItem*> FindChildren(const ImportedSource& source,
                                              const std::string& parent_id) {
  std::vector<const ImportedItem*> children;
  for (const auto& item : source.items) {
    if (item.parent_id == parent_id) children.push_back(&item);
  }
  std::sort(children.begin(), children.end(),
            [](const ImportedItem* a, const ImportedItem* b) {
              return a->order < b->order;
            });
  return children;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────
// Detection tests
// ─────────────────────────────────────────────────────────────────────────

class SafariDetectionTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::FilePath CreateSafariDir() {
    base::FilePath safari_dir =
        temp_dir_.GetPath().Append("Library").Append("Safari");
    base::CreateDirectory(safari_dir);
    return safari_dir;
  }

  base::ScopedTempDir temp_dir_;
};

TEST_F(SafariDetectionTest, MissingFile) {
  base::FilePath missing =
      temp_dir_.GetPath().Append("nonexistent").Append("Bookmarks.plist");
  auto result = SafariBookmarkParser::DetectProfile(missing);

  EXPECT_EQ("safari", result.browser);
  ASSERT_EQ(1u, result.profiles.size());
  EXPECT_EQ("default", result.profiles[0].directory_name);
  EXPECT_EQ("Bookmarks", result.profiles[0].display_name);
  EXPECT_EQ(ProfileAccessStatus::kNotFound,
            result.profiles[0].access_status);
  EXPECT_EQ(-1, result.profiles[0].bookmark_count);
}

TEST_F(SafariDetectionTest, EmptyBookmarks) {
  base::FilePath safari_dir = CreateSafariDir();
  base::FilePath plist = safari_dir.Append("Bookmarks.plist");
  WritePlist(MinimalBookmarksPlist(), plist);

  auto result = SafariBookmarkParser::DetectProfile(plist);

  ASSERT_EQ(1u, result.profiles.size());
  EXPECT_EQ(ProfileAccessStatus::kOk, result.profiles[0].access_status);
  EXPECT_EQ(0, result.profiles[0].bookmark_count);
}

TEST_F(SafariDetectionTest, BookmarkCountExcludesReadingListAndProxy) {
  base::FilePath safari_dir = CreateSafariDir();
  base::FilePath plist = safari_dir.Append("Bookmarks.plist");

  NSDictionary* root = @{
    @"WebBookmarkFileVersion" : @1,
    @"WebBookmarkType" : @"WebBookmarkTypeList",
    @"Title" : @"",
    @"WebBookmarkUUID" : @"ROOT",
    @"Children" : @[
      ProxyNode(@"History", @"History"),
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksBar",
        @"WebBookmarkUUID" : @"BAR",
        @"Children" : @[
          BookmarkLeaf(@"Apple", @"https://apple.com"),
          BookmarkLeaf(@"Google", @"https://google.com"),
        ],
      },
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksMenu",
        @"WebBookmarkUUID" : @"MENU",
        @"Children" : @[
          BookmarkLeaf(@"GitHub", @"https://github.com"),
        ],
      },
      ReadingListContainer(@[
        BookmarkLeaf(@"RL Article", @"https://example.com/article"),
        BookmarkLeaf(@"RL Tutorial", @"https://example.com/tutorial"),
      ]),
    ],
  };
  WritePlist(root, plist);

  auto result = SafariBookmarkParser::DetectProfile(plist);

  ASSERT_EQ(1u, result.profiles.size());
  EXPECT_EQ(ProfileAccessStatus::kOk, result.profiles[0].access_status);
  EXPECT_EQ(3, result.profiles[0].bookmark_count);
}

TEST_F(SafariDetectionTest, CorruptFile) {
  base::FilePath safari_dir = CreateSafariDir();
  base::FilePath plist = safari_dir.Append("Bookmarks.plist");
  base::WriteFile(plist, "this is not a plist");

  auto result = SafariBookmarkParser::DetectProfile(plist);

  ASSERT_EQ(1u, result.profiles.size());
  EXPECT_EQ(ProfileAccessStatus::kError,
            result.profiles[0].access_status);
  EXPECT_EQ(-1, result.profiles[0].bookmark_count);
}

// ─────────────────────────────────────────────────────────────────────────
// Parser tests
// ─────────────────────────────────────────────────────────────────────────

class SafariBookmarkParserTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::FilePath WritePlistFile(NSDictionary* dict) {
    base::FilePath safari_dir =
        temp_dir_.GetPath().Append("Safari");
    base::CreateDirectory(safari_dir);
    base::FilePath plist = safari_dir.Append("Bookmarks.plist");
    WritePlist(dict, plist);
    return plist;
  }

  base::ScopedTempDir temp_dir_;
};

TEST_F(SafariBookmarkParserTest, EmptyBookmarks) {
  base::FilePath plist = WritePlistFile(MinimalBookmarksPlist());
  auto result = SafariBookmarkParser::ParseBookmarksPlist(plist);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ("safari", result->source.browser);
  EXPECT_EQ("Bookmarks", result->source.profile_name);
  EXPECT_TRUE(result->source.items.empty());
  EXPECT_EQ(0, result->stats.links_seen);
  EXPECT_EQ(0, result->stats.links_imported);
  EXPECT_EQ(0, result->stats.folders_seen);
  EXPECT_EQ(0, result->stats.folders_imported);
  EXPECT_EQ(0, result->stats.items_skipped);
}

TEST_F(SafariBookmarkParserTest, BookmarksBarLinks) {
  NSDictionary* root = @{
    @"WebBookmarkFileVersion" : @1,
    @"WebBookmarkType" : @"WebBookmarkTypeList",
    @"Title" : @"",
    @"WebBookmarkUUID" : @"ROOT",
    @"Children" : @[
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksBar",
        @"WebBookmarkUUID" : @"BAR",
        @"Children" : @[
          BookmarkLeaf(@"Apple", @"https://apple.com"),
          BookmarkLeaf(@"Google", @"https://google.com"),
          BookmarkLeaf(@"GitHub", @"https://github.com"),
        ],
      },
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksMenu",
        @"WebBookmarkUUID" : @"MENU",
        @"Children" : @[],
      },
    ],
  };
  base::FilePath plist = WritePlistFile(root);
  auto result = SafariBookmarkParser::ParseBookmarksPlist(plist);

  ASSERT_TRUE(result.has_value());
  // 1 root folder (Favorites) + 3 links.
  EXPECT_EQ(4u, result->source.items.size());
  EXPECT_EQ(3, result->stats.links_seen);
  EXPECT_EQ(3, result->stats.links_imported);
  EXPECT_EQ(1, result->stats.folders_seen);
  EXPECT_EQ(1, result->stats.folders_imported);

  // Root folder should be named "Favorites" (BookmarksBar → Favorites).
  auto* favorites = FindByTitle(result->source, "Favorites");
  ASSERT_TRUE(favorites);
  EXPECT_EQ(ImportedItemType::kFolder, favorites->type);
  EXPECT_TRUE(favorites->parent_id.empty());

  // Links should be children of the Favorites folder.
  auto children = FindChildren(result->source, favorites->id);
  ASSERT_EQ(3u, children.size());
  EXPECT_EQ("Apple", children[0]->title);
  EXPECT_EQ("https://apple.com", children[0]->url);
  EXPECT_EQ("Google", children[1]->title);
  EXPECT_EQ("GitHub", children[2]->title);
}

TEST_F(SafariBookmarkParserTest, BookmarksMenuLinks) {
  NSDictionary* root = @{
    @"WebBookmarkFileVersion" : @1,
    @"WebBookmarkType" : @"WebBookmarkTypeList",
    @"Title" : @"",
    @"WebBookmarkUUID" : @"ROOT",
    @"Children" : @[
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksBar",
        @"WebBookmarkUUID" : @"BAR",
        @"Children" : @[],
      },
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksMenu",
        @"WebBookmarkUUID" : @"MENU",
        @"Children" : @[
          BookmarkLeaf(@"Reddit", @"https://reddit.com"),
          BookmarkLeaf(@"Wikipedia", @"https://wikipedia.org"),
        ],
      },
    ],
  };
  base::FilePath plist = WritePlistFile(root);
  auto result = SafariBookmarkParser::ParseBookmarksPlist(plist);

  ASSERT_TRUE(result.has_value());
  auto* menu = FindByTitle(result->source, "Bookmarks Menu");
  ASSERT_TRUE(menu);
  EXPECT_EQ(ImportedItemType::kFolder, menu->type);

  auto children = FindChildren(result->source, menu->id);
  ASSERT_EQ(2u, children.size());
  EXPECT_EQ("Reddit", children[0]->title);
  EXPECT_EQ("Wikipedia", children[1]->title);
}

TEST_F(SafariBookmarkParserTest, NestedFolders) {
  NSDictionary* root = @{
    @"WebBookmarkFileVersion" : @1,
    @"WebBookmarkType" : @"WebBookmarkTypeList",
    @"Title" : @"",
    @"WebBookmarkUUID" : @"ROOT",
    @"Children" : @[
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksBar",
        @"WebBookmarkUUID" : @"BAR",
        @"Children" : @[
          BookmarkFolder(@"Work", @[
            BookmarkLeaf(@"Jira", @"https://jira.com"),
            BookmarkFolder(@"AWS", @[
              BookmarkLeaf(@"Console", @"https://console.aws.amazon.com"),
              BookmarkLeaf(@"S3", @"https://s3.console.aws.amazon.com"),
            ]),
          ]),
          BookmarkLeaf(@"Gmail", @"https://gmail.com"),
        ],
      },
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksMenu",
        @"WebBookmarkUUID" : @"MENU",
        @"Children" : @[],
      },
    ],
  };
  base::FilePath plist = WritePlistFile(root);
  auto result = SafariBookmarkParser::ParseBookmarksPlist(plist);

  ASSERT_TRUE(result.has_value());
  // Favorites(folder) + Work(folder) + Jira + AWS(folder) + Console + S3 + Gmail = 7
  EXPECT_EQ(7u, result->source.items.size());
  EXPECT_EQ(3, result->stats.folders_seen);     // Favorites, Work, AWS
  EXPECT_EQ(3, result->stats.folders_imported);
  EXPECT_EQ(4, result->stats.links_seen);        // Jira, Console, S3, Gmail
  EXPECT_EQ(4, result->stats.links_imported);

  auto* favorites = FindByTitle(result->source, "Favorites");
  ASSERT_TRUE(favorites);

  auto fav_children = FindChildren(result->source, favorites->id);
  ASSERT_EQ(2u, fav_children.size());
  EXPECT_EQ("Work", fav_children[0]->title);
  EXPECT_EQ("Gmail", fav_children[1]->title);

  auto work_children = FindChildren(result->source, fav_children[0]->id);
  ASSERT_EQ(2u, work_children.size());
  EXPECT_EQ("Jira", work_children[0]->title);
  EXPECT_EQ("AWS", work_children[1]->title);

  auto aws_children = FindChildren(result->source, work_children[1]->id);
  ASSERT_EQ(2u, aws_children.size());
  EXPECT_EQ("Console", aws_children[0]->title);
  EXPECT_EQ("S3", aws_children[1]->title);
}

TEST_F(SafariBookmarkParserTest, ProxyNodesExcluded) {
  NSDictionary* root = @{
    @"WebBookmarkFileVersion" : @1,
    @"WebBookmarkType" : @"WebBookmarkTypeList",
    @"Title" : @"",
    @"WebBookmarkUUID" : @"ROOT",
    @"Children" : @[
      ProxyNode(@"History", @"History"),
      ProxyNode(@"Address Book", @"Address Book"),
      ProxyNode(@"Bonjour", @"Bonjour"),
      ProxyNode(@"All RSS Feeds", @"All RSS Feeds"),
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksBar",
        @"WebBookmarkUUID" : @"BAR",
        @"Children" : @[
          BookmarkLeaf(@"Apple", @"https://apple.com"),
        ],
      },
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksMenu",
        @"WebBookmarkUUID" : @"MENU",
        @"Children" : @[],
      },
    ],
  };
  base::FilePath plist = WritePlistFile(root);
  auto result = SafariBookmarkParser::ParseBookmarksPlist(plist);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(2u, result->source.items.size());  // Favorites folder + Apple
  EXPECT_EQ(0, result->stats.items_skipped);
}

TEST_F(SafariBookmarkParserTest, ReadingListExcluded) {
  NSDictionary* root = @{
    @"WebBookmarkFileVersion" : @1,
    @"WebBookmarkType" : @"WebBookmarkTypeList",
    @"Title" : @"",
    @"WebBookmarkUUID" : @"ROOT",
    @"Children" : @[
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksBar",
        @"WebBookmarkUUID" : @"BAR",
        @"Children" : @[
          BookmarkLeaf(@"Apple", @"https://apple.com"),
        ],
      },
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksMenu",
        @"WebBookmarkUUID" : @"MENU",
        @"Children" : @[],
      },
      ReadingListContainer(@[
        BookmarkLeaf(@"RL1", @"https://example.com/1"),
        BookmarkLeaf(@"RL2", @"https://example.com/2"),
        BookmarkLeaf(@"RL3", @"https://example.com/3"),
      ]),
    ],
  };
  base::FilePath plist = WritePlistFile(root);
  auto result = SafariBookmarkParser::ParseBookmarksPlist(plist);

  ASSERT_TRUE(result.has_value());
  // Only Favorites folder + Apple from BookmarksBar.
  EXPECT_EQ(2u, result->source.items.size());
  EXPECT_EQ(1, result->stats.links_imported);
  // Reading List items counted as excluded (not skipped).
  EXPECT_EQ(0, result->stats.items_skipped);
  EXPECT_EQ(3, result->stats.items_excluded);
}

TEST_F(SafariBookmarkParserTest, FavoritesHierarchyPreserved) {
  // Safari's "Favorites" are under BookmarksBar.  They should be imported
  // under a "Favorites" root folder, not converted to Avora Favorites.
  NSDictionary* root = @{
    @"WebBookmarkFileVersion" : @1,
    @"WebBookmarkType" : @"WebBookmarkTypeList",
    @"Title" : @"",
    @"WebBookmarkUUID" : @"ROOT",
    @"Children" : @[
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksBar",
        @"WebBookmarkUUID" : @"BAR",
        @"Children" : @[
          BookmarkLeaf(@"Favorite Site 1", @"https://fav1.com"),
          BookmarkFolder(@"My Favorites", @[
            BookmarkLeaf(@"Favorite Site 2", @"https://fav2.com"),
          ]),
        ],
      },
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksMenu",
        @"WebBookmarkUUID" : @"MENU",
        @"Children" : @[],
      },
    ],
  };
  base::FilePath plist = WritePlistFile(root);
  auto result = SafariBookmarkParser::ParseBookmarksPlist(plist);

  ASSERT_TRUE(result.has_value());
  // Favorites(folder) + Favorite Site 1 + My Favorites(folder) + Favorite Site 2
  EXPECT_EQ(4u, result->source.items.size());
  EXPECT_EQ(2, result->stats.links_imported);
  EXPECT_EQ(2, result->stats.folders_imported);

  auto* favorites_root = FindByTitle(result->source, "Favorites");
  ASSERT_TRUE(favorites_root);
  EXPECT_TRUE(favorites_root->parent_id.empty());

  auto children = FindChildren(result->source, favorites_root->id);
  ASSERT_EQ(2u, children.size());
  EXPECT_EQ("Favorite Site 1", children[0]->title);
  EXPECT_EQ("My Favorites", children[1]->title);
}

TEST_F(SafariBookmarkParserTest, InvalidUrlsSkipped) {
  NSDictionary* root = @{
    @"WebBookmarkFileVersion" : @1,
    @"WebBookmarkType" : @"WebBookmarkTypeList",
    @"Title" : @"",
    @"WebBookmarkUUID" : @"ROOT",
    @"Children" : @[
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksBar",
        @"WebBookmarkUUID" : @"BAR",
        @"Children" : @[
          BookmarkLeaf(@"Valid", @"https://example.com"),
          BookmarkLeaf(@"JavaScript", @"javascript:alert('hi')"),
          BookmarkLeaf(@"Data URL", @"data:text/html,<h1>test</h1>"),
          BookmarkLeaf(@"Mailto", @"mailto:user@example.com"),
          BookmarkLeaf(@"Blob", @"blob:https://example.com/abc"),
          BookmarkLeaf(@"Empty URL", @""),
          BookmarkLeaf(@"File URL", @"file:///Users/test/doc.html"),
          BookmarkLeaf(@"Chrome URL", @"chrome://settings"),
        ],
      },
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksMenu",
        @"WebBookmarkUUID" : @"MENU",
        @"Children" : @[],
      },
    ],
  };
  base::FilePath plist = WritePlistFile(root);
  auto result = SafariBookmarkParser::ParseBookmarksPlist(plist);

  ASSERT_TRUE(result.has_value());
  // Favorites(folder) + Valid + File URL + Chrome URL = 4 items.
  EXPECT_EQ(8, result->stats.links_seen);
  EXPECT_EQ(3, result->stats.links_imported);
  EXPECT_EQ(5, result->stats.items_skipped);

  EXPECT_TRUE(FindByTitle(result->source, "Valid") != nullptr);
  EXPECT_TRUE(FindByTitle(result->source, "File URL") != nullptr);
  EXPECT_TRUE(FindByTitle(result->source, "Chrome URL") != nullptr);
  EXPECT_TRUE(FindByTitle(result->source, "JavaScript") == nullptr);
  EXPECT_TRUE(FindByTitle(result->source, "Data URL") == nullptr);
  EXPECT_TRUE(FindByTitle(result->source, "Mailto") == nullptr);
}

TEST_F(SafariBookmarkParserTest, DuplicateUrlsPreserved) {
  NSDictionary* root = @{
    @"WebBookmarkFileVersion" : @1,
    @"WebBookmarkType" : @"WebBookmarkTypeList",
    @"Title" : @"",
    @"WebBookmarkUUID" : @"ROOT",
    @"Children" : @[
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksBar",
        @"WebBookmarkUUID" : @"BAR",
        @"Children" : @[
          BookmarkLeaf(@"GitHub 1", @"https://github.com"),
          BookmarkLeaf(@"GitHub 2", @"https://github.com"),
        ],
      },
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksMenu",
        @"WebBookmarkUUID" : @"MENU",
        @"Children" : @[
          BookmarkLeaf(@"GitHub 3", @"https://github.com"),
        ],
      },
    ],
  };
  base::FilePath plist = WritePlistFile(root);
  auto result = SafariBookmarkParser::ParseBookmarksPlist(plist);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(3, result->stats.links_imported);

  int github_count = 0;
  for (const auto& item : result->source.items) {
    if (item.url == "https://github.com") github_count++;
  }
  EXPECT_EQ(3, github_count);
}

TEST_F(SafariBookmarkParserTest, UnicodePreserved) {
  NSDictionary* root = @{
    @"WebBookmarkFileVersion" : @1,
    @"WebBookmarkType" : @"WebBookmarkTypeList",
    @"Title" : @"",
    @"WebBookmarkUUID" : @"ROOT",
    @"Children" : @[
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksBar",
        @"WebBookmarkUUID" : @"BAR",
        @"Children" : @[
          BookmarkLeaf(@"日本語サイト", @"https://example.jp"),
          BookmarkLeaf(@"Ñoño", @"https://example.es"),
          BookmarkLeaf(@"🚀 Rocket", @"https://rocket.dev"),
          BookmarkFolder(@"中文文件夹", @[
            BookmarkLeaf(@"百度", @"https://baidu.com"),
          ]),
        ],
      },
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksMenu",
        @"WebBookmarkUUID" : @"MENU",
        @"Children" : @[],
      },
    ],
  };
  base::FilePath plist = WritePlistFile(root);
  auto result = SafariBookmarkParser::ParseBookmarksPlist(plist);

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(FindByTitle(result->source, "日本語サイト") != nullptr);
  EXPECT_TRUE(FindByTitle(result->source, "Ñoño") != nullptr);
  EXPECT_TRUE(FindByTitle(result->source, "🚀 Rocket") != nullptr);
  EXPECT_TRUE(FindByTitle(result->source, "中文文件夹") != nullptr);
  EXPECT_TRUE(FindByTitle(result->source, "百度") != nullptr);
}

TEST_F(SafariBookmarkParserTest, SiblingOrdering) {
  NSDictionary* root = @{
    @"WebBookmarkFileVersion" : @1,
    @"WebBookmarkType" : @"WebBookmarkTypeList",
    @"Title" : @"",
    @"WebBookmarkUUID" : @"ROOT",
    @"Children" : @[
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksBar",
        @"WebBookmarkUUID" : @"BAR",
        @"Children" : @[
          BookmarkLeaf(@"First", @"https://first.com"),
          BookmarkLeaf(@"Second", @"https://second.com"),
          BookmarkLeaf(@"Third", @"https://third.com"),
          BookmarkLeaf(@"Fourth", @"https://fourth.com"),
        ],
      },
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksMenu",
        @"WebBookmarkUUID" : @"MENU",
        @"Children" : @[],
      },
    ],
  };
  base::FilePath plist = WritePlistFile(root);
  auto result = SafariBookmarkParser::ParseBookmarksPlist(plist);

  ASSERT_TRUE(result.has_value());
  auto* favorites = FindByTitle(result->source, "Favorites");
  ASSERT_TRUE(favorites);

  auto children = FindChildren(result->source, favorites->id);
  ASSERT_EQ(4u, children.size());
  EXPECT_EQ("First", children[0]->title);
  EXPECT_EQ("Second", children[1]->title);
  EXPECT_EQ("Third", children[2]->title);
  EXPECT_EQ("Fourth", children[3]->title);
}

TEST_F(SafariBookmarkParserTest, MissingFile) {
  base::FilePath missing =
      temp_dir_.GetPath().Append("nonexistent.plist");
  auto result = SafariBookmarkParser::ParseBookmarksPlist(missing);
  EXPECT_FALSE(result.has_value());
}

TEST_F(SafariBookmarkParserTest, CorruptFile) {
  base::FilePath safari_dir =
      temp_dir_.GetPath().Append("Safari");
  base::CreateDirectory(safari_dir);
  base::FilePath plist = safari_dir.Append("Bookmarks.plist");
  base::WriteFile(plist, "not a valid plist file at all");

  auto result = SafariBookmarkParser::ParseBookmarksPlist(plist);
  EXPECT_FALSE(result.has_value());
}

TEST_F(SafariBookmarkParserTest, NotBookmarksPlist) {
  // Valid plist but not a Safari bookmarks file.
  base::FilePath safari_dir =
      temp_dir_.GetPath().Append("Safari");
  base::CreateDirectory(safari_dir);
  base::FilePath plist = safari_dir.Append("Bookmarks.plist");

  NSDictionary* not_bookmarks = @{
    @"SomeOtherKey" : @"value",
  };
  WritePlist(not_bookmarks, plist);

  auto result = SafariBookmarkParser::ParseBookmarksPlist(plist);
  EXPECT_FALSE(result.has_value());
}

TEST_F(SafariBookmarkParserTest, BothRootsWithContent) {
  NSDictionary* root = @{
    @"WebBookmarkFileVersion" : @1,
    @"WebBookmarkType" : @"WebBookmarkTypeList",
    @"Title" : @"",
    @"WebBookmarkUUID" : @"ROOT",
    @"Children" : @[
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksBar",
        @"WebBookmarkUUID" : @"BAR",
        @"Children" : @[
          BookmarkLeaf(@"Bar Link", @"https://bar.com"),
        ],
      },
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksMenu",
        @"WebBookmarkUUID" : @"MENU",
        @"Children" : @[
          BookmarkLeaf(@"Menu Link", @"https://menu.com"),
        ],
      },
    ],
  };
  base::FilePath plist = WritePlistFile(root);
  auto result = SafariBookmarkParser::ParseBookmarksPlist(plist);

  ASSERT_TRUE(result.has_value());
  // 2 root folders + 2 links.
  EXPECT_EQ(4u, result->source.items.size());
  EXPECT_EQ(2, result->stats.folders_imported);  // Favorites + Bookmarks Menu
  EXPECT_EQ(2, result->stats.links_imported);

  auto* favorites = FindByTitle(result->source, "Favorites");
  ASSERT_TRUE(favorites);
  EXPECT_EQ(0, favorites->order);

  auto* menu = FindByTitle(result->source, "Bookmarks Menu");
  ASSERT_TRUE(menu);
  EXPECT_EQ(1, menu->order);
}

TEST_F(SafariBookmarkParserTest, MixedValidInvalidStats) {
  NSDictionary* root = @{
    @"WebBookmarkFileVersion" : @1,
    @"WebBookmarkType" : @"WebBookmarkTypeList",
    @"Title" : @"",
    @"WebBookmarkUUID" : @"ROOT",
    @"Children" : @[
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksBar",
        @"WebBookmarkUUID" : @"BAR",
        @"Children" : @[
          BookmarkLeaf(@"Valid 1", @"https://v1.com"),
          BookmarkLeaf(@"Invalid 1", @"javascript:void(0)"),
          BookmarkLeaf(@"Valid 2", @"https://v2.com"),
          BookmarkLeaf(@"Invalid 2", @"data:text/html,test"),
          BookmarkLeaf(@"Invalid 3", @"mailto:test@test.com"),
        ],
      },
      @{
        @"WebBookmarkType" : @"WebBookmarkTypeList",
        @"Title" : @"BookmarksMenu",
        @"WebBookmarkUUID" : @"MENU",
        @"Children" : @[
          BookmarkLeaf(@"Valid 3", @"https://v3.com"),
        ],
      },
      ReadingListContainer(@[
        BookmarkLeaf(@"RL 1", @"https://rl1.com"),
        BookmarkLeaf(@"RL 2", @"https://rl2.com"),
      ]),
    ],
  };
  base::FilePath plist = WritePlistFile(root);
  auto result = SafariBookmarkParser::ParseBookmarksPlist(plist);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(6, result->stats.links_seen);
  EXPECT_EQ(3, result->stats.links_imported);
  EXPECT_EQ(2, result->stats.folders_seen);
  EXPECT_EQ(2, result->stats.folders_imported);
  // 3 invalid URLs are skipped; 2 Reading List items are excluded.
  EXPECT_EQ(3, result->stats.items_skipped);
  EXPECT_EQ(2, result->stats.items_excluded);
}

// ─────────────────────────────────────────────────────────────────────────
// Source identity tests
// ─────────────────────────────────────────────────────────────────────────

class SafariSourceIdentityTest : public testing::Test {};

TEST_F(SafariSourceIdentityTest, DeterministicId) {
  std::string id1 = AvoraImportCoordinator::GenerateImportedSourceId(
      "safari", "default", "space-1");
  std::string id2 = AvoraImportCoordinator::GenerateImportedSourceId(
      "safari", "default", "space-1");
  EXPECT_EQ(id1, id2);
}

TEST_F(SafariSourceIdentityTest, DifferentSpacesDifferentIds) {
  std::string id1 = AvoraImportCoordinator::GenerateImportedSourceId(
      "safari", "default", "space-work");
  std::string id2 = AvoraImportCoordinator::GenerateImportedSourceId(
      "safari", "default", "space-personal");
  EXPECT_NE(id1, id2);
}

TEST_F(SafariSourceIdentityTest, DifferentBrowsersDifferentIds) {
  std::string safari_id = AvoraImportCoordinator::GenerateImportedSourceId(
      "safari", "default", "space-1");
  std::string chrome_id = AvoraImportCoordinator::GenerateImportedSourceId(
      "chrome", "Default", "space-1");
  std::string edge_id = AvoraImportCoordinator::GenerateImportedSourceId(
      "edge", "Default", "space-1");
  std::string firefox_id = AvoraImportCoordinator::GenerateImportedSourceId(
      "firefox", "abc.default-release", "space-1");

  EXPECT_NE(safari_id, chrome_id);
  EXPECT_NE(safari_id, edge_id);
  EXPECT_NE(safari_id, firefox_id);
}

// ─────────────────────────────────────────────────────────────────────────
// Cross-browser coexistence tests
// ─────────────────────────────────────────────────────────────────────────

class SafariCoexistenceTest : public testing::Test {};

TEST_F(SafariCoexistenceTest, AllFourBrowsersCoexist) {
  // Verify that Safari + Chrome + Edge + Firefox in the same Space
  // all generate distinct source IDs.
  std::string space = "same-space";
  std::string safari = AvoraImportCoordinator::GenerateImportedSourceId(
      "safari", "default", space);
  std::string chrome = AvoraImportCoordinator::GenerateImportedSourceId(
      "chrome", "Default", space);
  std::string edge = AvoraImportCoordinator::GenerateImportedSourceId(
      "edge", "Default", space);
  std::string firefox = AvoraImportCoordinator::GenerateImportedSourceId(
      "firefox", "abc.default-release", space);

  // All four must be unique.
  std::vector<std::string> ids = {safari, chrome, edge, firefox};
  for (size_t i = 0; i < ids.size(); i++) {
    for (size_t j = i + 1; j < ids.size(); j++) {
      EXPECT_NE(ids[i], ids[j]) << "Collision between browser " << i
                                  << " and " << j;
    }
  }
}

TEST_F(SafariCoexistenceTest, ReimportSameSpaceReplaces) {
  // The same triple produces the same ID, enabling replacement.
  std::string id1 = AvoraImportCoordinator::GenerateImportedSourceId(
      "safari", "default", "work");
  std::string id2 = AvoraImportCoordinator::GenerateImportedSourceId(
      "safari", "default", "work");
  EXPECT_EQ(id1, id2);
}

TEST_F(SafariCoexistenceTest, SameSafariDifferentSpacesCoexist) {
  std::string work = AvoraImportCoordinator::GenerateImportedSourceId(
      "safari", "default", "work");
  std::string personal = AvoraImportCoordinator::GenerateImportedSourceId(
      "safari", "default", "personal");
  EXPECT_NE(work, personal);
}

}  // namespace avora

#endif  // BUILDFLAG(IS_MAC)
