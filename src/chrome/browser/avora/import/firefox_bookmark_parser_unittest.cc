// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/import/firefox_bookmark_parser.h"

#include <set>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "chrome/browser/avora/avora_imported_links.h"
#include "chrome/browser/avora/import/avora_import_coordinator.h"
#include "chrome/browser/avora/import/bookmark_import_types.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace avora {
namespace {

using ::testing::IsEmpty;
using ::testing::SizeIs;

// ── Test helper: build a Firefox places.sqlite in memory ────────────────────

class FirefoxDbBuilder {
 public:
  explicit FirefoxDbBuilder(const base::FilePath& db_path) : path_(db_path) {
    EXPECT_TRUE(db_.Open(db_path));
    CreateSchema();
    InsertRoots();
  }

  // Insert a bookmark (type=1).
  void AddBookmark(int64_t id, int64_t parent, int position,
                   const std::string& title, const std::string& url) {
    // Insert into moz_places first.
    sql::Statement places(db_.GetUniqueStatement(
        "INSERT INTO moz_places (id, url) VALUES (?, ?)"));
    places.BindInt64(0, id + 1000);  // Offset place IDs.
    places.BindString(1, url);
    EXPECT_TRUE(places.Run());

    sql::Statement stmt(db_.GetUniqueStatement(
        "INSERT INTO moz_bookmarks (id, type, fk, parent, position, title, "
        "guid) VALUES (?, 1, ?, ?, ?, ?, ?)"));
    stmt.BindInt64(0, id);
    stmt.BindInt64(1, id + 1000);
    stmt.BindInt64(2, parent);
    stmt.BindInt(3, position);
    stmt.BindString(4, title);
    stmt.BindString(5, "guid" + std::to_string(id));
    EXPECT_TRUE(stmt.Run());
  }

  // Insert a folder (type=2).
  int64_t AddFolder(int64_t id, int64_t parent, int position,
                    const std::string& title,
                    const std::string& guid = "") {
    sql::Statement stmt(db_.GetUniqueStatement(
        "INSERT INTO moz_bookmarks (id, type, parent, position, title, guid) "
        "VALUES (?, 2, ?, ?, ?, ?)"));
    stmt.BindInt64(0, id);
    stmt.BindInt64(1, parent);
    stmt.BindInt(2, position);
    stmt.BindString(3, title);
    stmt.BindString(4, guid.empty() ? "guid" + std::to_string(id) : guid);
    EXPECT_TRUE(stmt.Run());
    return id;
  }

  // Insert a separator (type=3).
  void AddSeparator(int64_t id, int64_t parent, int position) {
    sql::Statement stmt(db_.GetUniqueStatement(
        "INSERT INTO moz_bookmarks (id, type, parent, position, title, guid) "
        "VALUES (?, 3, ?, ?, '', ?)"));
    stmt.BindInt64(0, id);
    stmt.BindInt64(1, parent);
    stmt.BindInt(2, position);
    stmt.BindString(3, "sep" + std::to_string(id));
    EXPECT_TRUE(stmt.Run());
  }

  void Close() { db_.Close(); }

  // Firefox well-known IDs.
  static constexpr int64_t kRootId = 1;
  static constexpr int64_t kMenuId = 2;
  static constexpr int64_t kToolbarId = 3;
  static constexpr int64_t kTagsId = 4;
  static constexpr int64_t kUnfiledId = 5;
  static constexpr int64_t kMobileId = 6;

 private:
  void CreateSchema() {
    EXPECT_TRUE(db_.Execute(
        "CREATE TABLE moz_places ("
        "  id INTEGER PRIMARY KEY,"
        "  url LONGVARCHAR,"
        "  title LONGVARCHAR,"
        "  rev_host LONGVARCHAR,"
        "  visit_count INTEGER DEFAULT 0,"
        "  hidden INTEGER DEFAULT 0,"
        "  typed INTEGER DEFAULT 0,"
        "  frecency INTEGER DEFAULT -1,"
        "  last_visit_date INTEGER,"
        "  guid TEXT"
        ")"));

    EXPECT_TRUE(db_.Execute(
        "CREATE TABLE moz_bookmarks ("
        "  id INTEGER PRIMARY KEY,"
        "  type INTEGER,"
        "  fk INTEGER DEFAULT NULL,"
        "  parent INTEGER,"
        "  position INTEGER,"
        "  title LONGVARCHAR,"
        "  keyword_id INTEGER,"
        "  folder_type TEXT,"
        "  dateAdded INTEGER,"
        "  lastModified INTEGER,"
        "  guid TEXT,"
        "  syncStatus INTEGER NOT NULL DEFAULT 0,"
        "  syncChangeCounter INTEGER NOT NULL DEFAULT 1"
        ")"));
  }

  void InsertRoots() {
    AddFolder(kRootId, 0, 0, "", "root________");
    AddFolder(kMenuId, kRootId, 0, "Bookmarks Menu", "menu________");
    AddFolder(kToolbarId, kRootId, 1, "Bookmarks Toolbar", "toolbar_____");
    AddFolder(kTagsId, kRootId, 2, "Tags", "tags________");
    AddFolder(kUnfiledId, kRootId, 3, "Other Bookmarks", "unfiled_____");
    AddFolder(kMobileId, kRootId, 4, "Mobile Bookmarks", "mobile______");
  }

  base::FilePath path_;
  sql::Database db_;
};

// ── Helpers ─────────────────────────────────────────────────────────────────

std::vector<ImportedItem> LinksOnly(const ImportedSource& source) {
  std::vector<ImportedItem> links;
  for (const auto& item : source.items) {
    if (item.type == ImportedItemType::kLink) links.push_back(item);
  }
  return links;
}

std::vector<ImportedItem> FoldersOnly(const ImportedSource& source) {
  std::vector<ImportedItem> folders;
  for (const auto& item : source.items) {
    if (item.type == ImportedItemType::kFolder) folders.push_back(item);
  }
  return folders;
}

std::vector<ImportedItem> ChildrenOf(const ImportedSource& source,
                                     const std::string& parent_id) {
  std::vector<ImportedItem> children;
  for (const auto& item : source.items) {
    if (item.parent_id == parent_id) children.push_back(item);
  }
  std::stable_sort(children.begin(), children.end(),
                   [](const ImportedItem& a, const ImportedItem& b) {
                     return a.order < b.order;
                   });
  return children;
}

// ── Test fixture ────────────────────────────────────────────────────────────

class FirefoxBookmarkParserTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::FilePath CreateDb(const std::string& name = "places.sqlite") {
    return temp_dir_.GetPath().AppendASCII(name);
  }

  base::ScopedTempDir temp_dir_;
};

// ── Profile detection tests ─────────────────────────────────────────────────

class FirefoxProfileDetectionTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::FilePath CreateFirefoxDataDir() {
    base::FilePath data_dir = temp_dir_.GetPath().AppendASCII("Firefox");
    base::CreateDirectory(data_dir);
    return data_dir;
  }

  void WriteProfilesIni(const base::FilePath& data_dir,
                        const std::string& content) {
    base::WriteFile(data_dir.AppendASCII("profiles.ini"), content);
  }

  void CreateProfileDir(const base::FilePath& data_dir,
                        const std::string& dir_name,
                        bool with_places = true) {
    base::FilePath profile_dir = data_dir.AppendASCII(dir_name);
    base::CreateDirectory(profile_dir);
    if (with_places) {
      // Create a minimal places.sqlite with some bookmarks.
      base::FilePath db_path = profile_dir.AppendASCII("places.sqlite");
      FirefoxDbBuilder builder(db_path);
      builder.AddBookmark(100, FirefoxDbBuilder::kToolbarId, 0,
                          "GitHub", "https://github.com");
      builder.AddBookmark(101, FirefoxDbBuilder::kToolbarId, 1,
                          "Gmail", "https://mail.google.com");
      builder.Close();
    }
  }

  base::ScopedTempDir temp_dir_;
};

TEST_F(FirefoxProfileDetectionTest, SingleProfile) {
  auto data_dir = CreateFirefoxDataDir();
  CreateProfileDir(data_dir, "abc12345.default-release");

  WriteProfilesIni(data_dir, R"(
[General]
StartWithLastProfile=1
Version=2

[Profile0]
Name=default-release
IsRelative=1
Path=abc12345.default-release
Default=1
)");

  auto detected = FirefoxBookmarkParser::DetectProfiles(data_dir);

  EXPECT_EQ(detected.browser, "firefox");
  ASSERT_THAT(detected.profiles, SizeIs(1));
  EXPECT_EQ(detected.profiles[0].directory_name, "abc12345.default-release");
  EXPECT_EQ(detected.profiles[0].display_name, "default-release");
  EXPECT_EQ(detected.profiles[0].bookmark_count, 2);
}

TEST_F(FirefoxProfileDetectionTest, MultipleProfiles) {
  auto data_dir = CreateFirefoxDataDir();
  CreateProfileDir(data_dir, "abc12345.default-release");
  CreateProfileDir(data_dir, "def67890.work");

  WriteProfilesIni(data_dir, R"(
[General]
StartWithLastProfile=1
Version=2

[Profile0]
Name=default-release
IsRelative=1
Path=abc12345.default-release
Default=1

[Profile1]
Name=Work
IsRelative=1
Path=def67890.work
)");

  auto detected = FirefoxBookmarkParser::DetectProfiles(data_dir);

  ASSERT_THAT(detected.profiles, SizeIs(2));
  EXPECT_EQ(detected.profiles[0].directory_name, "abc12345.default-release");
  EXPECT_EQ(detected.profiles[0].display_name, "default-release");
  EXPECT_EQ(detected.profiles[1].directory_name, "def67890.work");
  EXPECT_EQ(detected.profiles[1].display_name, "Work");
}

TEST_F(FirefoxProfileDetectionTest, StableSourceProfileId) {
  auto data_dir = CreateFirefoxDataDir();
  CreateProfileDir(data_dir, "abc12345.default-release");

  WriteProfilesIni(data_dir, R"(
[Profile0]
Name=My Custom Name
IsRelative=1
Path=abc12345.default-release
)");

  auto detected = FirefoxBookmarkParser::DetectProfiles(data_dir);

  ASSERT_THAT(detected.profiles, SizeIs(1));
  // directory_name (source_profile_id) is the actual directory, not the
  // user-visible Name.
  EXPECT_EQ(detected.profiles[0].directory_name, "abc12345.default-release");
  EXPECT_EQ(detected.profiles[0].display_name, "My Custom Name");
}

TEST_F(FirefoxProfileDetectionTest, MissingProfilesIni) {
  auto data_dir = CreateFirefoxDataDir();
  // No profiles.ini written.
  auto detected = FirefoxBookmarkParser::DetectProfiles(data_dir);

  EXPECT_EQ(detected.browser, "firefox");
  EXPECT_THAT(detected.profiles, IsEmpty());
}

TEST_F(FirefoxProfileDetectionTest, NonexistentDataDir) {
  base::FilePath fake = temp_dir_.GetPath().AppendASCII("NoFirefox");
  auto detected = FirefoxBookmarkParser::DetectProfiles(fake);

  EXPECT_EQ(detected.browser, "firefox");
  EXPECT_THAT(detected.profiles, IsEmpty());
}

TEST_F(FirefoxProfileDetectionTest, ProfileDirMissing) {
  auto data_dir = CreateFirefoxDataDir();
  // Profile listed in INI but directory doesn't exist.
  WriteProfilesIni(data_dir, R"(
[Profile0]
Name=ghost
IsRelative=1
Path=ghost12345.default
)");

  auto detected = FirefoxBookmarkParser::DetectProfiles(data_dir);

  EXPECT_THAT(detected.profiles, IsEmpty());
}

TEST_F(FirefoxProfileDetectionTest, ProfileWithNoPlacesDb) {
  auto data_dir = CreateFirefoxDataDir();
  CreateProfileDir(data_dir, "nodb12345.default", /*with_places=*/false);

  WriteProfilesIni(data_dir, R"(
[Profile0]
Name=empty
IsRelative=1
Path=nodb12345.default
)");

  auto detected = FirefoxBookmarkParser::DetectProfiles(data_dir);

  ASSERT_THAT(detected.profiles, SizeIs(1));
  // No places.sqlite → bookmark_count stays at default -1.
  EXPECT_EQ(detected.profiles[0].bookmark_count, -1);
}

// ── Database parsing tests ──────────────────────────────────────────────────

TEST_F(FirefoxBookmarkParserTest, EmptyDatabase) {
  auto db_path = CreateDb();
  {
    FirefoxDbBuilder builder(db_path);
    // Roots only, no user bookmarks.
    builder.Close();
  }

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabaseDirect(db_path, "Default");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->source.browser, "firefox");
  EXPECT_THAT(result->source.items, IsEmpty());
  EXPECT_EQ(result->stats.links_seen, 0);
  EXPECT_EQ(result->stats.links_imported, 0);
  EXPECT_EQ(result->stats.items_skipped, 0);
}

TEST_F(FirefoxBookmarkParserTest, ToolbarBookmarks) {
  auto db_path = CreateDb();
  {
    FirefoxDbBuilder builder(db_path);
    builder.AddBookmark(100, FirefoxDbBuilder::kToolbarId, 0,
                        "GitHub", "https://github.com");
    builder.AddBookmark(101, FirefoxDbBuilder::kToolbarId, 1,
                        "Gmail", "https://mail.google.com");
    builder.Close();
  }

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabaseDirect(db_path, "Default");

  ASSERT_TRUE(result.has_value());
  const auto& source = result->source;

  auto folders = FoldersOnly(source);
  ASSERT_THAT(folders, SizeIs(1));
  EXPECT_EQ(folders[0].title, "Bookmarks Toolbar");

  auto links = LinksOnly(source);
  ASSERT_THAT(links, SizeIs(2));
  EXPECT_EQ(links[0].title, "GitHub");
  EXPECT_EQ(links[0].url, "https://github.com");
  EXPECT_EQ(links[1].title, "Gmail");
  EXPECT_EQ(links[1].url, "https://mail.google.com");
}

TEST_F(FirefoxBookmarkParserTest, AllUserContentRoots) {
  auto db_path = CreateDb();
  {
    FirefoxDbBuilder builder(db_path);
    builder.AddBookmark(100, FirefoxDbBuilder::kMenuId, 0,
                        "Menu Link", "https://menu.com");
    builder.AddBookmark(101, FirefoxDbBuilder::kToolbarId, 0,
                        "Toolbar Link", "https://toolbar.com");
    builder.AddBookmark(102, FirefoxDbBuilder::kUnfiledId, 0,
                        "Unfiled Link", "https://unfiled.com");
    builder.AddBookmark(103, FirefoxDbBuilder::kMobileId, 0,
                        "Mobile Link", "https://mobile.com");
    builder.Close();
  }

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabaseDirect(db_path, "Default");

  ASSERT_TRUE(result.has_value());
  const auto& source = result->source;

  auto folders = FoldersOnly(source);
  ASSERT_THAT(folders, SizeIs(4));

  std::set<std::string> folder_titles;
  for (const auto& f : folders) folder_titles.insert(f.title);
  EXPECT_TRUE(folder_titles.count("Bookmarks Menu"));
  EXPECT_TRUE(folder_titles.count("Bookmarks Toolbar"));
  EXPECT_TRUE(folder_titles.count("Other Bookmarks"));
  EXPECT_TRUE(folder_titles.count("Mobile Bookmarks"));

  auto links = LinksOnly(source);
  EXPECT_THAT(links, SizeIs(4));
}

TEST_F(FirefoxBookmarkParserTest, NestedFolders) {
  auto db_path = CreateDb();
  {
    FirefoxDbBuilder builder(db_path);
    // Toolbar → Work (folder) → Jira, AWS
    int64_t work_id = builder.AddFolder(
        100, FirefoxDbBuilder::kToolbarId, 0, "Work");
    builder.AddBookmark(101, work_id, 0, "Jira", "https://jira.com");
    builder.AddBookmark(102, work_id, 1, "AWS", "https://aws.com");

    // Toolbar → GitHub (direct link)
    builder.AddBookmark(103, FirefoxDbBuilder::kToolbarId, 1,
                        "GitHub", "https://github.com");
    builder.Close();
  }

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabaseDirect(db_path, "Default");

  ASSERT_TRUE(result.has_value());
  const auto& source = result->source;

  // Root: Bookmarks Toolbar → Work (folder), GitHub (link)
  auto root_folders = FoldersOnly(source);
  // Bookmarks Toolbar + Work = 2 folders
  EXPECT_THAT(root_folders, SizeIs(2));

  auto links = LinksOnly(source);
  EXPECT_THAT(links, SizeIs(3));  // Jira, AWS, GitHub

  // Verify hierarchy.
  auto root_children = ChildrenOf(source, "");
  ASSERT_THAT(root_children, SizeIs(1));
  EXPECT_EQ(root_children[0].title, "Bookmarks Toolbar");

  auto toolbar_children = ChildrenOf(source, root_children[0].id);
  ASSERT_THAT(toolbar_children, SizeIs(2));
  EXPECT_EQ(toolbar_children[0].title, "Work");
  EXPECT_EQ(toolbar_children[0].type, ImportedItemType::kFolder);
  EXPECT_EQ(toolbar_children[1].title, "GitHub");
  EXPECT_EQ(toolbar_children[1].type, ImportedItemType::kLink);

  auto work_children = ChildrenOf(source, toolbar_children[0].id);
  ASSERT_THAT(work_children, SizeIs(2));
  EXPECT_EQ(work_children[0].title, "Jira");
  EXPECT_EQ(work_children[1].title, "AWS");
}

TEST_F(FirefoxBookmarkParserTest, SiblingOrderPreserved) {
  auto db_path = CreateDb();
  {
    FirefoxDbBuilder builder(db_path);
    builder.AddBookmark(100, FirefoxDbBuilder::kToolbarId, 0,
                        "First", "https://first.com");
    builder.AddBookmark(101, FirefoxDbBuilder::kToolbarId, 1,
                        "Second", "https://second.com");
    builder.AddBookmark(102, FirefoxDbBuilder::kToolbarId, 2,
                        "Third", "https://third.com");
    builder.Close();
  }

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabaseDirect(db_path, "Default");

  ASSERT_TRUE(result.has_value());
  auto links = LinksOnly(result->source);
  ASSERT_THAT(links, SizeIs(3));
  EXPECT_EQ(links[0].title, "First");
  EXPECT_EQ(links[0].order, 0);
  EXPECT_EQ(links[1].title, "Second");
  EXPECT_EQ(links[1].order, 1);
  EXPECT_EQ(links[2].title, "Third");
  EXPECT_EQ(links[2].order, 2);
}

TEST_F(FirefoxBookmarkParserTest, DuplicateUrlsPreserved) {
  auto db_path = CreateDb();
  {
    FirefoxDbBuilder builder(db_path);
    builder.AddBookmark(100, FirefoxDbBuilder::kToolbarId, 0,
                        "GitHub (1)", "https://github.com");
    builder.AddBookmark(101, FirefoxDbBuilder::kMenuId, 0,
                        "GitHub (2)", "https://github.com");
    builder.Close();
  }

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabaseDirect(db_path, "Default");

  ASSERT_TRUE(result.has_value());
  auto links = LinksOnly(result->source);
  EXPECT_THAT(links, SizeIs(2));

  int github_count = 0;
  for (const auto& l : links) {
    if (l.url == "https://github.com") ++github_count;
  }
  EXPECT_EQ(github_count, 2);
}

TEST_F(FirefoxBookmarkParserTest, UnicodeTitles) {
  auto db_path = CreateDb();
  {
    FirefoxDbBuilder builder(db_path);
    builder.AddBookmark(100, FirefoxDbBuilder::kToolbarId, 0,
                        "日本語テスト", "https://japanese.test");
    builder.AddBookmark(101, FirefoxDbBuilder::kToolbarId, 1,
                        "Emoji 🚀", "https://emoji.test");
    builder.Close();
  }

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabaseDirect(db_path, "Default");

  ASSERT_TRUE(result.has_value());
  auto links = LinksOnly(result->source);
  ASSERT_THAT(links, SizeIs(2));
  EXPECT_EQ(links[0].title, "日本語テスト");
  EXPECT_EQ(links[1].title, "Emoji 🚀");
}

TEST_F(FirefoxBookmarkParserTest, InvalidUrlsSkipped) {
  auto db_path = CreateDb();
  {
    FirefoxDbBuilder builder(db_path);
    builder.AddBookmark(100, FirefoxDbBuilder::kToolbarId, 0,
                        "Good", "https://good.com");
    builder.AddBookmark(101, FirefoxDbBuilder::kToolbarId, 1,
                        "Data URL", "data:text/html,hi");
    builder.AddBookmark(102, FirefoxDbBuilder::kToolbarId, 2,
                        "Javascript", "javascript:void(0)");
    builder.AddBookmark(103, FirefoxDbBuilder::kToolbarId, 3,
                        "Mailto", "mailto:user@example.com");
    builder.AddBookmark(104, FirefoxDbBuilder::kToolbarId, 4,
                        "Also Good", "https://also-good.com");
    builder.Close();
  }

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabaseDirect(db_path, "Default");

  ASSERT_TRUE(result.has_value());
  auto links = LinksOnly(result->source);
  EXPECT_THAT(links, SizeIs(2));
  EXPECT_EQ(links[0].title, "Good");
  EXPECT_EQ(links[1].title, "Also Good");
}

TEST_F(FirefoxBookmarkParserTest, SeparatorsSkipped) {
  auto db_path = CreateDb();
  {
    FirefoxDbBuilder builder(db_path);
    builder.AddBookmark(100, FirefoxDbBuilder::kToolbarId, 0,
                        "Before", "https://before.com");
    builder.AddSeparator(101, FirefoxDbBuilder::kToolbarId, 1);
    builder.AddBookmark(102, FirefoxDbBuilder::kToolbarId, 2,
                        "After", "https://after.com");
    builder.Close();
  }

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabaseDirect(db_path, "Default");

  ASSERT_TRUE(result.has_value());
  auto links = LinksOnly(result->source);
  EXPECT_THAT(links, SizeIs(2));
  EXPECT_EQ(result->stats.items_skipped, 1);
}

TEST_F(FirefoxBookmarkParserTest, TagsExcluded) {
  auto db_path = CreateDb();
  {
    FirefoxDbBuilder builder(db_path);
    builder.AddBookmark(100, FirefoxDbBuilder::kToolbarId, 0,
                        "Real Bookmark", "https://real.com");
    // Create a tag folder under the Tags root — should be excluded.
    int64_t tag_folder = builder.AddFolder(
        200, FirefoxDbBuilder::kTagsId, 0, "my-tag");
    builder.AddBookmark(201, tag_folder, 0,
                        "Tagged", "https://tagged.com");
    builder.Close();
  }

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabaseDirect(db_path, "Default");

  ASSERT_TRUE(result.has_value());
  auto links = LinksOnly(result->source);
  EXPECT_THAT(links, SizeIs(1));
  EXPECT_EQ(links[0].title, "Real Bookmark");
}

TEST_F(FirefoxBookmarkParserTest, MissingDatabase) {
  base::FilePath missing = temp_dir_.GetPath().AppendASCII("nonexistent.db");
  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabaseDirect(missing, "Default");

  EXPECT_FALSE(result.has_value());
}

TEST_F(FirefoxBookmarkParserTest, MalformedDatabase) {
  base::FilePath bad_db = temp_dir_.GetPath().AppendASCII("bad.db");
  base::WriteFile(bad_db, "this is not a sqlite database");

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabaseDirect(bad_db, "Default");

  EXPECT_FALSE(result.has_value());
}

// ── Authoritative ParseStats tests ──────────────────────────────────────────

TEST_F(FirefoxBookmarkParserTest, StatsToolbarOnly) {
  auto db_path = CreateDb();
  {
    FirefoxDbBuilder builder(db_path);
    builder.AddBookmark(100, FirefoxDbBuilder::kToolbarId, 0,
                        "Link 1", "https://one.com");
    builder.AddBookmark(101, FirefoxDbBuilder::kToolbarId, 1,
                        "Link 2", "https://two.com");
    builder.Close();
  }

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabaseDirect(db_path, "Default");

  ASSERT_TRUE(result.has_value());
  const auto& stats = result->stats;
  EXPECT_EQ(stats.links_seen, 2);
  EXPECT_EQ(stats.links_imported, 2);
  EXPECT_EQ(stats.folders_seen, 1);   // Bookmarks Toolbar
  EXPECT_EQ(stats.folders_imported, 1);
  EXPECT_EQ(stats.items_skipped, 0);
}

TEST_F(FirefoxBookmarkParserTest, StatsMixedValidInvalid) {
  auto db_path = CreateDb();
  {
    FirefoxDbBuilder builder(db_path);
    builder.AddBookmark(100, FirefoxDbBuilder::kToolbarId, 0,
                        "Good", "https://good.com");
    builder.AddBookmark(101, FirefoxDbBuilder::kToolbarId, 1,
                        "Bad", "javascript:alert(1)");
    builder.AddSeparator(102, FirefoxDbBuilder::kToolbarId, 2);
    builder.AddBookmark(103, FirefoxDbBuilder::kToolbarId, 3,
                        "Also Good", "https://also.com");
    builder.Close();
  }

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabaseDirect(db_path, "Default");

  ASSERT_TRUE(result.has_value());
  const auto& stats = result->stats;
  EXPECT_EQ(stats.links_seen, 3);     // Good, Bad, Also Good
  EXPECT_EQ(stats.links_imported, 2); // Good, Also Good
  EXPECT_EQ(stats.folders_seen, 1);   // Bookmarks Toolbar
  EXPECT_EQ(stats.folders_imported, 1);
  EXPECT_EQ(stats.items_skipped, 2);  // Bad URL + separator
}

TEST_F(FirefoxBookmarkParserTest, StatsConsistency) {
  auto db_path = CreateDb();
  {
    FirefoxDbBuilder builder(db_path);
    int64_t work = builder.AddFolder(
        100, FirefoxDbBuilder::kToolbarId, 0, "Work");
    builder.AddBookmark(101, work, 0, "Jira", "https://jira.com");
    builder.AddBookmark(102, work, 1, "Bad", "data:text/plain,x");
    builder.AddBookmark(103, FirefoxDbBuilder::kMenuId, 0,
                        "Menu Link", "https://menu.com");
    builder.Close();
  }

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabaseDirect(db_path, "Default");

  ASSERT_TRUE(result.has_value());
  const auto& stats = result->stats;
  const auto& source = result->source;

  EXPECT_EQ(
      static_cast<size_t>(stats.links_imported + stats.folders_imported),
      source.items.size());
}

// ── Source identity tests ───────────────────────────────────────────────────

TEST(FirefoxSourceIdentityTest, FirefoxChromeEdgeDistinct) {
  const std::string firefox_id =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "firefox", "abc12345.default-release", "space-1");
  const std::string chrome_id =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "chrome", "Default", "space-1");
  const std::string edge_id =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "edge", "Default", "space-1");

  EXPECT_NE(firefox_id, chrome_id);
  EXPECT_NE(firefox_id, edge_id);
  EXPECT_NE(chrome_id, edge_id);
}

TEST(FirefoxSourceIdentityTest, SameFirefoxProfileTwoSpaces) {
  const std::string id_1 =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "firefox", "abc12345.default-release", "space-work");
  const std::string id_2 =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "firefox", "abc12345.default-release", "space-personal");

  EXPECT_NE(id_1, id_2);
}

TEST(FirefoxSourceIdentityTest, SameFirefoxProfileSameSpaceDeterministic) {
  const std::string id_a =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "firefox", "abc12345.default-release", "space-x");
  const std::string id_b =
      AvoraImportCoordinator::GenerateImportedSourceId(
          "firefox", "abc12345.default-release", "space-x");

  EXPECT_EQ(id_a, id_b);
}

// ── Safe-read strategy tests ────────────────────────────────────────────────

TEST_F(FirefoxBookmarkParserTest, ParsePlacesDatabaseDirectAccess) {
  // The public ParsePlacesDatabase API should successfully read a
  // database via its read-only primary path.
  auto db_path = CreateDb();
  {
    FirefoxDbBuilder builder(db_path);
    builder.AddBookmark(100, FirefoxDbBuilder::kToolbarId, 0,
                        "Test", "https://test.com");
    builder.Close();
  }

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabase(db_path, "Default");

  ASSERT_TRUE(result.has_value());
  auto links = LinksOnly(result->source);
  EXPECT_THAT(links, SizeIs(1));
  EXPECT_EQ(links[0].title, "Test");
}

TEST_F(FirefoxBookmarkParserTest, OriginalDatabaseUnmodified) {
  // Verify that the import process does not modify the source database.
  auto db_path = CreateDb();
  {
    FirefoxDbBuilder builder(db_path);
    builder.AddBookmark(100, FirefoxDbBuilder::kToolbarId, 0,
                        "Test", "https://test.com");
    builder.Close();
  }

  // Record the file's modification time and size before import.
  base::File::Info info_before;
  ASSERT_TRUE(base::GetFileInfo(db_path, &info_before));

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabase(db_path, "Default");
  ASSERT_TRUE(result.has_value());

  // Verify file metadata is unchanged after import.
  base::File::Info info_after;
  ASSERT_TRUE(base::GetFileInfo(db_path, &info_after));

  EXPECT_EQ(info_before.size, info_after.size);
  EXPECT_EQ(info_before.last_modified, info_after.last_modified);
}

TEST_F(FirefoxBookmarkParserTest, WalFileAbsent) {
  // When there is no WAL file (Firefox was cleanly shut down or
  // checkpointed), the primary read-only path should still work.
  auto db_path = CreateDb();
  {
    FirefoxDbBuilder builder(db_path);
    builder.AddBookmark(100, FirefoxDbBuilder::kToolbarId, 0,
                        "NoWal", "https://nowal.com");
    builder.Close();
  }

  // Explicitly verify no WAL/SHM files exist.
  EXPECT_FALSE(base::PathExists(
      db_path.DirName().AppendASCII("places.sqlite-wal")));
  EXPECT_FALSE(base::PathExists(
      db_path.DirName().AppendASCII("places.sqlite-shm")));

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabase(db_path, "Default");

  ASSERT_TRUE(result.has_value());
  EXPECT_THAT(LinksOnly(result->source), SizeIs(1));
}

TEST_F(FirefoxBookmarkParserTest, WalFilePresent) {
  // When a WAL file exists (Firefox was running or not yet
  // checkpointed), import should still succeed.  We simulate this by
  // opening the database in WAL mode and writing without checkpointing.
  auto db_path = CreateDb();
  {
    FirefoxDbBuilder builder(db_path);
    builder.AddBookmark(100, FirefoxDbBuilder::kToolbarId, 0,
                        "BeforeWal", "https://beforewal.com");
    builder.Close();
  }

  // Re-open in WAL mode and add a bookmark without checkpointing.
  {
    sql::Database db(sql::Database::Tag{"test"});
    ASSERT_TRUE(db.Open(db_path));
    ASSERT_TRUE(db.Execute("PRAGMA journal_mode=WAL"));

    sql::Statement places(db.GetUniqueStatement(
        "INSERT INTO moz_places (id, url) VALUES (2000, "
        "'https://afterwal.com')"));
    ASSERT_TRUE(places.Run());

    sql::Statement stmt(db.GetUniqueStatement(
        "INSERT INTO moz_bookmarks (id, type, fk, parent, position, title, "
        "guid) VALUES (200, 1, 2000, 3, 1, 'AfterWal', 'guidwal200')"));
    ASSERT_TRUE(stmt.Run());
    // Close without checkpointing — data lives in WAL.
  }

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabase(db_path, "Default");

  ASSERT_TRUE(result.has_value());
  auto links = LinksOnly(result->source);
  // Should see BOTH bookmarks: the pre-WAL one and the in-WAL one.
  EXPECT_THAT(links, SizeIs(2));
}

TEST_F(FirefoxBookmarkParserTest, CorruptDatabase) {
  // A corrupt database should return nullopt from both primary and
  // fallback paths.
  base::FilePath corrupt = temp_dir_.GetPath().AppendASCII("corrupt.db");
  base::WriteFile(corrupt, "not a valid sqlite database at all");

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabase(corrupt, "Default");

  EXPECT_FALSE(result.has_value());
}

TEST_F(FirefoxBookmarkParserTest, MissingDatabaseFile) {
  base::FilePath missing = temp_dir_.GetPath().AppendASCII("gone.db");

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabase(missing, "Default");

  EXPECT_FALSE(result.has_value());
}

TEST_F(FirefoxBookmarkParserTest, DatabaseMissingRequiredTables) {
  // A database that exists and opens but doesn't have the expected
  // Firefox tables should return nullopt.
  auto db_path = CreateDb("incomplete.sqlite");
  {
    sql::Database db(sql::Database::Tag{"test"});
    ASSERT_TRUE(db.Open(db_path));
    ASSERT_TRUE(db.Execute("CREATE TABLE some_other_table (id INTEGER)"));
  }

  auto result =
      FirefoxBookmarkParser::ParsePlacesDatabaseDirect(db_path, "Default");

  EXPECT_FALSE(result.has_value());
}

}  // namespace
}  // namespace avora
