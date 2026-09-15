// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/import/firefox_bookmark_parser.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "build/build_config.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "url/gurl.h"

namespace avora {

namespace {

// ── Firefox constants ───────────────────────────────────────────────────────

constexpr char kProfilesIni[] = "profiles.ini";
constexpr char kPlacesSqlite[] = "places.sqlite";

// moz_bookmarks.type constants from Firefox source.
constexpr int kTypeBookmark = 1;
constexpr int kTypeFolder = 2;
constexpr int kTypeSeparator = 3;

// Well-known root folder GUIDs in Firefox.
constexpr char kRootGuid[] = "root________";
constexpr char kMenuGuid[] = "menu________";
constexpr char kToolbarGuid[] = "toolbar_____";
constexpr char kUnfiledGuid[] = "unfiled_____";
constexpr char kMobileGuid[] = "mobile______";
constexpr char kTagsGuid[] = "tags________";

// ── Helpers ─────────────────────────────────────────────────────────────────

std::string GenerateId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

bool IsValidUrl(const std::string& url_string) {
  if (url_string.empty()) {
    return false;
  }
  GURL url(url_string);
  return url.is_valid() && (url.SchemeIsHTTPOrHTTPS() ||
                            url.SchemeIs("file") ||
                            url.SchemeIs("chrome") ||
                            url.SchemeIs("ftp"));
}

// Maps Firefox root GUIDs to user-friendly folder names.
std::string RootFolderTitle(const std::string& guid) {
  if (guid == kMenuGuid) return "Bookmarks Menu";
  if (guid == kToolbarGuid) return "Bookmarks Toolbar";
  if (guid == kUnfiledGuid) return "Other Bookmarks";
  if (guid == kMobileGuid) return "Mobile Bookmarks";
  return std::string();
}

// Returns true if this root folder should be shown to the user.
bool IsUserContentRoot(const std::string& guid) {
  return guid == kMenuGuid || guid == kToolbarGuid ||
         guid == kUnfiledGuid || guid == kMobileGuid;
}

// ── profiles.ini parsing ────────────────────────────────────────────────────

struct FirefoxProfileEntry {
  std::string name;
  std::string path;
  bool is_relative = true;
};

// Parses Firefox's profiles.ini file format.
// Returns a list of profile entries with their Name and Path.
std::vector<FirefoxProfileEntry> ParseProfilesIni(
    const std::string& ini_content) {
  std::vector<FirefoxProfileEntry> profiles;
  FirefoxProfileEntry* current = nullptr;

  for (const auto& line :
       base::SplitStringPiece(ini_content, "\n",
                              base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    if (line.empty() || line[0] == '#' || line[0] == ';') {
      continue;
    }

    if (line[0] == '[') {
      // Section header.
      std::string section(line);
      if (base::StartsWith(section, "[Profile",
                           base::CompareCase::INSENSITIVE_ASCII)) {
        profiles.emplace_back();
        current = &profiles.back();
      } else {
        current = nullptr;
      }
      continue;
    }

    if (!current) continue;

    size_t eq = line.find('=');
    if (eq == std::string_view::npos) continue;

    std::string key(line.substr(0, eq));
    std::string value(line.substr(eq + 1));

    if (key == "Name") {
      current->name = value;
    } else if (key == "Path") {
      current->path = value;
    } else if (key == "IsRelative") {
      current->is_relative = (value == "1");
    }
  }

  return profiles;
}

// ── Database reading ────────────────────────────────────────────────────────

// Row data from a single moz_bookmarks query.
struct BookmarkRow {
  int64_t id = 0;
  int type = 0;
  int64_t parent = 0;
  int position = 0;
  std::string title;
  std::string url;   // Resolved from moz_places via fk.
  std::string guid;
};

// Reads all bookmark rows from the database.
std::vector<BookmarkRow> ReadBookmarkRows(sql::Database& db) {
  std::vector<BookmarkRow> rows;

  static constexpr char kSql[] =
      "SELECT b.id, b.type, b.parent, b.position, b.title, b.guid, "
      "       p.url "
      "FROM moz_bookmarks b "
      "LEFT JOIN moz_places p ON b.fk = p.id "
      "ORDER BY b.parent, b.position";

  sql::Statement stmt(db.GetUniqueStatement(kSql));
  while (stmt.Step()) {
    BookmarkRow row;
    row.id = stmt.ColumnInt64(0);
    row.type = stmt.ColumnInt(1);
    row.parent = stmt.ColumnInt64(2);
    row.position = stmt.ColumnInt(3);
    row.title = stmt.ColumnString(4);
    row.guid = stmt.ColumnString(5);
    row.url = stmt.ColumnString(6);
    rows.push_back(std::move(row));
  }

  return rows;
}

// Counts the total number of bookmark URLs in the database (for
// DetectedProfile::bookmark_count).
int CountBookmarksInDb(sql::Database& db) {
  static constexpr char kSql[] =
      "SELECT COUNT(*) FROM moz_bookmarks WHERE type = 1";
  sql::Statement stmt(db.GetUniqueStatement(kSql));
  if (stmt.Step()) {
    return stmt.ColumnInt(0);
  }
  return 0;
}

// Converts Firefox bookmark rows into a flat list of ImportedItems,
// tracking stats at each decision point.
void ConvertRows(const std::vector<BookmarkRow>& rows,
                 std::vector<ImportedItem>& items,
                 ParseStats& stats) {
  // Build lookup: Firefox id → row index.
  std::map<int64_t, size_t> id_to_index;
  for (size_t i = 0; i < rows.size(); i++) {
    id_to_index[rows[i].id] = i;
  }

  // Identify the Firefox internal root (guid = "root________") and
  // the tags root (guid = "tags________").
  int64_t root_id = -1;
  int64_t tags_id = -1;
  for (const auto& row : rows) {
    if (row.guid == kRootGuid) root_id = row.id;
    if (row.guid == kTagsGuid) tags_id = row.id;
  }

  // Map Firefox id → Avora id for parent resolution.
  std::map<int64_t, std::string> firefox_to_avora_id;

  // Group rows by parent, preserving position order (already sorted).
  std::map<int64_t, std::vector<size_t>> children_of;
  for (size_t i = 0; i < rows.size(); i++) {
    children_of[rows[i].parent].push_back(i);
  }

  // Skip the tags root and all its descendants.
  std::map<int64_t, bool> skip_subtree;
  if (tags_id >= 0) {
    skip_subtree[tags_id] = true;
  }

  // Process user content root folders (children of the Places root).
  // These become top-level Avora folders.
  auto root_children_it = children_of.find(root_id);
  if (root_children_it == children_of.end()) {
    return;
  }

  int root_order = 0;
  for (size_t idx : root_children_it->second) {
    const BookmarkRow& row = rows[idx];
    if (row.type != kTypeFolder) continue;
    if (!IsUserContentRoot(row.guid)) continue;
    if (skip_subtree.count(row.id)) continue;

    // Check if this root has any children.
    auto children_it = children_of.find(row.id);
    if (children_it == children_of.end() || children_it->second.empty()) {
      continue;
    }

    stats.folders_seen++;
    ImportedItem root_folder;
    root_folder.id = GenerateId();
    root_folder.type = ImportedItemType::kFolder;
    root_folder.title = RootFolderTitle(row.guid);
    if (root_folder.title.empty()) {
      root_folder.title = row.title.empty() ? "Bookmarks" : row.title;
    }
    root_folder.order = root_order++;
    firefox_to_avora_id[row.id] = root_folder.id;
    const std::string root_folder_id = root_folder.id;
    items.push_back(std::move(root_folder));
    stats.folders_imported++;

    // Process children recursively using a stack.
    struct StackEntry {
      int64_t firefox_id;
      std::string avora_parent_id;
    };
    std::vector<StackEntry> stack;
    stack.push_back({row.id, root_folder_id});

    while (!stack.empty()) {
      StackEntry entry = stack.back();
      stack.pop_back();

      auto it = children_of.find(entry.firefox_id);
      if (it == children_of.end()) continue;

      int sibling_order = 0;
      for (size_t child_idx : it->second) {
        const BookmarkRow& child = rows[child_idx];

        if (skip_subtree.count(child.id)) continue;

        if (child.type == kTypeBookmark) {
          stats.links_seen++;
          if (child.url.empty() || !IsValidUrl(child.url)) {
            stats.items_skipped++;
            continue;
          }

          ImportedItem item;
          item.id = GenerateId();
          item.parent_id = entry.avora_parent_id;
          item.type = ImportedItemType::kLink;
          item.title = child.title;
          item.url = child.url;
          item.order = sibling_order++;
          firefox_to_avora_id[child.id] = item.id;
          items.push_back(std::move(item));
          stats.links_imported++;

        } else if (child.type == kTypeFolder) {
          stats.folders_seen++;
          ImportedItem folder;
          folder.id = GenerateId();
          folder.parent_id = entry.avora_parent_id;
          folder.type = ImportedItemType::kFolder;
          folder.title = child.title.empty() ? "(Untitled Folder)" : child.title;
          folder.order = sibling_order++;
          firefox_to_avora_id[child.id] = folder.id;
          const std::string folder_id = folder.id;
          items.push_back(std::move(folder));
          stats.folders_imported++;

          stack.push_back({child.id, folder_id});

        } else if (child.type == kTypeSeparator) {
          stats.items_skipped++;
        } else {
          stats.items_skipped++;
        }
      }
    }
  }
}

inline constexpr sql::Database::Tag kDatabaseTag{"FirefoxImporter"};

// Opens a SQLite database and parses its bookmarks.
// |use_read_only| controls whether the database is opened with
// SQLITE_OPEN_READONLY (true) or default read-write mode (false).
// Read-only mode is preferred for the live Firefox database; read-write
// mode is used for temp copies and test databases.
std::optional<ParseResult> ParseFromDb(const base::FilePath& db_path,
                                       const std::string& profile_name,
                                       bool use_read_only) {
  sql::DatabaseOptions options;
  if (use_read_only) {
    options.set_read_only(true);
    options.set_exclusive_locking(false);
  }

  sql::Database db(options, kDatabaseTag);
  if (!db.Open(db_path)) {
    LOG(WARNING) << "Failed to open Firefox places database: " << db_path;
    return std::nullopt;
  }

  if (!db.DoesTableExist("moz_bookmarks") ||
      !db.DoesTableExist("moz_places")) {
    LOG(WARNING) << "Firefox database missing required tables";
    return std::nullopt;
  }

  // Use sql::Transaction for RAII snapshot isolation.  In WAL mode,
  // Begin() issues BEGIN DEFERRED which captures a consistent snapshot
  // — all subsequent reads see the database as of this instant,
  // regardless of concurrent Firefox writes.
  sql::Transaction transaction(&db);
  if (!transaction.Begin()) {
    LOG(WARNING) << "Failed to begin read transaction on Firefox database";
    return std::nullopt;
  }

  auto rows = ReadBookmarkRows(db);

  // Commit releases the read lock.  For a read-only transaction,
  // Commit and Rollback are equivalent, but Commit is semantically
  // correct for a successful read.
  transaction.Commit();

  if (rows.empty()) {
    ParseResult result;
    result.source.id = GenerateId();
    result.source.browser = "firefox";
    result.source.profile_name = profile_name;
    result.source.imported_at = base::Time::Now();
    return result;
  }

  ParseResult result;
  result.source.id = GenerateId();
  result.source.browser = "firefox";
  result.source.profile_name = profile_name;
  result.source.imported_at = base::Time::Now();

  ConvertRows(rows, result.source.items, result.stats);

  return result;
}

// Counts bookmarks via a read-only connection to the live database.
// Returns -1 if the database cannot be opened.
int CountBookmarksReadOnly(const base::FilePath& db_path) {
  sql::DatabaseOptions options;
  options.set_read_only(true);
  options.set_exclusive_locking(false);

  sql::Database db(options, kDatabaseTag);
  if (!db.Open(db_path) || !db.DoesTableExist("moz_bookmarks")) {
    return -1;
  }
  return CountBookmarksInDb(db);
}

// Counts bookmarks by copying the database to a temp directory first.
// Used as a fallback when direct read-only access fails.
int CountBookmarksFallback(const base::FilePath& db_path) {
  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDir()) {
    return -1;
  }
  base::FilePath temp_db = temp_dir.GetPath().AppendASCII(kPlacesSqlite);
  if (!base::CopyFile(db_path, temp_db)) {
    return -1;
  }
  sql::Database db(kDatabaseTag);
  if (!db.Open(temp_db) || !db.DoesTableExist("moz_bookmarks")) {
    return -1;
  }
  return CountBookmarksInDb(db);
}

}  // namespace

// ── Public API ──────────────────────────────────────────────────────────────

// static
base::FilePath FirefoxBookmarkParser::DefaultFirefoxDataDir() {
#if BUILDFLAG(IS_MAC)
  base::FilePath home;
  if (base::PathExists(base::FilePath("/Users"))) {
    home = base::GetHomeDir();
  }
  return home.Append("Library")
      .Append("Application Support")
      .Append("Firefox");
#elif BUILDFLAG(IS_WIN)
  base::FilePath home = base::GetHomeDir();
  return home.Append(FILE_PATH_LITERAL("AppData"))
      .Append(FILE_PATH_LITERAL("Roaming"))
      .Append(FILE_PATH_LITERAL("Mozilla"))
      .Append(FILE_PATH_LITERAL("Firefox"));
#elif BUILDFLAG(IS_LINUX)
  // Firefox 147+ supports XDG, but falls back to legacy ~/.mozilla/firefox.
  // Check both locations, preferring legacy if it exists.
  base::FilePath home = base::GetHomeDir();
  base::FilePath legacy = home.Append(".mozilla").Append("firefox");
  if (base::DirectoryExists(legacy)) {
    return legacy;
  }
  base::FilePath xdg =
      home.Append(".config").Append("mozilla").Append("firefox");
  if (base::DirectoryExists(xdg)) {
    return xdg;
  }
  return legacy;  // Default to legacy path.
#else
  return base::FilePath();
#endif
}

// static
DetectedBrowser FirefoxBookmarkParser::DetectProfiles(
    const base::FilePath& data_dir) {
  DetectedBrowser result;
  result.browser = "firefox";
  result.data_dir = data_dir;

  if (!base::DirectoryExists(data_dir)) {
    return result;
  }

  // Read and parse profiles.ini.
  const base::FilePath ini_path = data_dir.AppendASCII(kProfilesIni);
  std::string ini_content;
  if (!base::ReadFileToString(ini_path, &ini_content)) {
    return result;
  }

  auto entries = ParseProfilesIni(ini_content);
  if (entries.empty()) {
    return result;
  }

  for (const auto& entry : entries) {
    if (entry.path.empty()) continue;

    base::FilePath profile_path;
    if (entry.is_relative) {
      profile_path = data_dir.AppendASCII(entry.path);
    } else {
      profile_path = base::FilePath::FromUTF8Unsafe(entry.path);
    }

    if (!base::DirectoryExists(profile_path)) continue;

    DetectedProfile profile;
    // Use the directory name (e.g. "abc12345.default-release") as
    // the stable profile identity, not the user-visible Name.
    profile.directory_name = profile_path.BaseName().AsUTF8Unsafe();
    profile.display_name = entry.name.empty() ? profile.directory_name
                                              : entry.name;
    profile.profile_path = profile_path;
    profile.bookmarks_path = profile_path.AppendASCII(kPlacesSqlite);

    // Count bookmarks if the database exists.
    // Primary: open the live database read-only (zero modification).
    // Fallback: copy the main .sqlite file to a temp dir and read it.
    if (base::PathExists(profile.bookmarks_path)) {
      int count = CountBookmarksReadOnly(profile.bookmarks_path);
      if (count < 0) {
        count = CountBookmarksFallback(profile.bookmarks_path);
      }
      profile.bookmark_count = count;
    }

    result.profiles.push_back(std::move(profile));
  }

  return result;
}

// static
std::optional<ParseResult> FirefoxBookmarkParser::ParsePlacesDatabase(
    const base::FilePath& places_sqlite_path,
    const std::string& profile_name) {
  // ── Primary approach: open the live database read-only ──────────
  //
  // In WAL mode, SQLite supports concurrent readers and writers.  A
  // read-only connection with a BEGIN DEFERRED transaction sees a
  // consistent snapshot including all WAL data checkpointed or not.
  //
  // This path:
  //   - Modifies zero bytes of the Firefox profile
  //   - Sees the most current data (including uncheckpointed WAL)
  //   - Provides transactional snapshot isolation
  auto result =
      ParseFromDb(places_sqlite_path, profile_name, /*use_read_only=*/true);
  if (result.has_value()) {
    return result;
  }

  // ── Fallback: copy only the main .sqlite file ──────────────────
  //
  // If direct read-only access fails (e.g. database busy/locked in
  // exclusive mode, permissions, or corrupt WAL), copy the main
  // database file to a temp directory and open the copy.
  //
  // We deliberately do NOT copy the WAL or SHM files:
  //   - The main .sqlite is always in a self-consistent state
  //     (at its last checkpoint boundary)
  //   - Copying .sqlite + .sqlite-wal independently is NOT atomic;
  //     Firefox can write between the two copies, producing an
  //     inconsistent pair
  //   - This matches Chromium's own Firefox import pattern in
  //     chrome/utility/importer/firefox_importer.cc which copies
  //     only places.sqlite via GetCopiedSourcePath()
  //
  // Trade-off: the copy may miss recent uncheckpointed writes, but
  // it is always consistent.
  LOG(INFO) << "Direct read-only access failed; falling back to file copy";

  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDir()) {
    LOG(WARNING) << "Failed to create temp directory for Firefox import";
    return std::nullopt;
  }

  base::FilePath temp_db = temp_dir.GetPath().AppendASCII(kPlacesSqlite);
  if (!base::CopyFile(places_sqlite_path, temp_db)) {
    LOG(WARNING) << "Failed to copy Firefox places database";
    return std::nullopt;
  }

  return ParseFromDb(temp_db, profile_name, /*use_read_only=*/false);
}

// static
std::optional<ParseResult> FirefoxBookmarkParser::ParsePlacesDatabaseDirect(
    const base::FilePath& db_path,
    const std::string& profile_name) {
  return ParseFromDb(db_path, profile_name, /*use_read_only=*/false);
}

}  // namespace avora
