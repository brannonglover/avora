// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_IMPORT_CHROMIUM_BOOKMARK_PARSER_H_
#define CHROME_BROWSER_AVORA_IMPORT_CHROMIUM_BOOKMARK_PARSER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "chrome/browser/avora/avora_imported_links.h"
#include "chrome/browser/avora/import/bookmark_import_types.h"

namespace avora {

// Authoritative statistics collected during a parse operation.
// Counts are incremented at the exact decision point where each node
// is accepted or discarded — not inferred from other quantities.
//
// These are operation/reporting metadata and are intentionally kept
// outside ImportedSource (they are not persisted).
struct ParseStats {
  int links_seen = 0;       // URL-type nodes encountered
  int links_imported = 0;   // URL nodes that passed validation
  int folders_seen = 0;     // Folder-type nodes encountered
  int folders_imported = 0; // Folders that were created in the output
  int items_skipped = 0;    // Nodes discarded (invalid URL, missing data, etc.)
  int items_excluded = 0;   // Nodes intentionally outside import scope
                             // (e.g. Safari Reading List, Firefox Tags)
};

// Wraps a parsed ImportedSource together with its parse statistics.
struct ParseResult {
  ImportedSource source;
  ParseStats stats;
};

// Detects Chromium-family browser profiles and parses their Bookmarks
// files into ImportedSource objects.
//
// All Chromium-based browsers (Chrome, Edge, Brave, etc.) share the
// same Bookmarks JSON format, profile directory layout, and Local State
// metadata.  This class handles the common parsing/detection logic;
// browser-specific callers supply the data directory and browser
// identifier.
//
// This class is stateless — each method is a pure function that reads
// from disk and returns structured data.  It never writes to prefs or
// interacts with ImportedLinkStore.  The caller is responsible for
// assigning a destination space_id and persisting the result.
class ChromiumBookmarkParser {
 public:
  // Default user-data directories for supported Chromium-based browsers.
  static base::FilePath DefaultChromeDataDir();
  static base::FilePath DefaultEdgeDataDir();

  // Scans |data_dir| for profile directories that contain a Bookmarks
  // file.  Reads profile display names from Local State if available.
  //
  // |browser| is the identifier written into ImportedSource.browser
  // (e.g. "chrome", "edge").
  //
  // Profiles with no Bookmarks file are included in the result with
  // bookmark_count = -1 so the caller can distinguish "no file" from
  // "file with zero bookmarks".
  //
  // Guest profiles are excluded by default (typically empty).
  static DetectedBrowser DetectProfiles(
      const base::FilePath& data_dir,
      const std::string& browser = "chrome");

  // Parses a single Bookmarks JSON file and converts it to a
  // ParseResult containing the ImportedSource and authoritative
  // parse statistics.  The returned source has an empty space_id —
  // the caller must assign one before persisting.
  //
  // |browser| and |profile_name| are passed through to the returned
  // ImportedSource metadata.
  //
  // Returns std::nullopt only when the file cannot be read or the JSON
  // is fundamentally unparseable (not valid JSON, or missing the
  // required "roots" object).  Individual malformed bookmarks within
  // an otherwise valid file are counted in stats.items_skipped rather
  // than failing the entire import.
  static std::optional<ParseResult> ParseBookmarksFile(
      const base::FilePath& bookmarks_path,
      const std::string& browser,
      const std::string& profile_name);

  // Convenience: DetectProfiles + ParseBookmarksFile for each detected
  // profile.  Returns one ImportedSource per profile that had parseable
  // bookmarks.  All returned sources have empty space_id.
  // Statistics are not propagated; use ParseBookmarksFile directly if
  // stats are needed.
  static std::vector<ImportedSource> ImportAllProfiles(
      const base::FilePath& data_dir,
      const std::string& browser = "chrome");

  // Parses bookmarks from a JSON string (rather than a file).  Useful
  // for testing without filesystem access.
  static std::optional<ParseResult> ParseBookmarksJson(
      const std::string& json_string,
      const std::string& browser,
      const std::string& profile_name);
};

// Backward-compatible alias — existing code that references
// ChromeBookmarkParser continues to compile.
using ChromeBookmarkParser = ChromiumBookmarkParser;

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_IMPORT_CHROMIUM_BOOKMARK_PARSER_H_
