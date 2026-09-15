// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_IMPORT_FIREFOX_BOOKMARK_PARSER_H_
#define CHROME_BROWSER_AVORA_IMPORT_FIREFOX_BOOKMARK_PARSER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "chrome/browser/avora/avora_imported_links.h"
#include "chrome/browser/avora/import/bookmark_import_types.h"
#include "chrome/browser/avora/import/chromium_bookmark_parser.h"  // ParseStats, ParseResult

namespace avora {

// Detects Firefox profiles and parses their bookmark databases into
// ParseResult objects.
//
// Firefox stores bookmarks in a SQLite database (places.sqlite) rather
// than a JSON file.  This class handles the Firefox-specific storage
// format while producing the same Avora-facing ParseResult/ParseStats
// output as ChromiumBookmarkParser.
//
// Safety: places.sqlite uses WAL mode and may be locked while Firefox
// is running.  This class uses a two-tier safe-read strategy:
//
//   Primary:  Open the live database read-only (SQLITE_OPEN_READONLY)
//             with locking_mode=NORMAL and a BEGIN DEFERRED transaction
//             for WAL snapshot isolation.  This sees all data including
//             uncheckpointed WAL content, and modifies zero bytes of
//             the Firefox profile.
//
//   Fallback: If direct access fails (exclusive lock, permissions,
//             corrupt WAL), copy only the main .sqlite file to a temp
//             directory and read the copy.  WAL/SHM files are NOT
//             copied because independent sequential copies are not
//             atomic and can produce an inconsistent pair.  The main
//             file alone is always in a self-consistent state at its
//             last checkpoint boundary.
//
// This matches the pattern used by Chromium's own Firefox importer in
// chrome/utility/importer/firefox_importer.cc (GetCopiedSourcePath).
//
// This class is stateless — each method is a pure function.  It never
// modifies the Firefox profile.
class FirefoxBookmarkParser {
 public:
  // Default Firefox application-data directory on the current platform.
  static base::FilePath DefaultFirefoxDataDir();

  // Scans the Firefox data directory for profiles by parsing
  // profiles.ini.  Returns a DetectedBrowser with browser = "firefox".
  //
  // source_profile_id (stored in DetectedProfile::directory_name) is
  // the profile directory name (e.g. "abc12345.default-release") —
  // stable across renames of the user-visible display name.
  static DetectedBrowser DetectProfiles(const base::FilePath& data_dir);

  // Parses bookmarks from a Firefox places.sqlite file.
  //
  // Uses the two-tier safe-read strategy described in the class
  // comment: read-only direct access first, file-copy fallback if
  // that fails.  Safe to call while Firefox is running.
  //
  // Returns std::nullopt if the database cannot be read or is
  // fundamentally corrupt.  Individual malformed bookmarks are counted
  // in stats.items_skipped.
  static std::optional<ParseResult> ParsePlacesDatabase(
      const base::FilePath& places_sqlite_path,
      const std::string& profile_name);

  // Parses bookmarks from an already-opened SQLite database at the
  // given path.  Does NOT copy the file — used for testing with
  // pre-built databases where locking is not a concern.
  //
  // The database must contain moz_bookmarks and moz_places tables.
  static std::optional<ParseResult> ParsePlacesDatabaseDirect(
      const base::FilePath& db_path,
      const std::string& profile_name);
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_IMPORT_FIREFOX_BOOKMARK_PARSER_H_
