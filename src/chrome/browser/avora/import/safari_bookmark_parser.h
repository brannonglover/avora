// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_IMPORT_SAFARI_BOOKMARK_PARSER_H_
#define CHROME_BROWSER_AVORA_IMPORT_SAFARI_BOOKMARK_PARSER_H_

#include "build/build_config.h"

#if BUILDFLAG(IS_MAC)

#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "chrome/browser/avora/import/bookmark_import_types.h"
#include "chrome/browser/avora/import/chromium_bookmark_parser.h"  // ParseResult

namespace avora {

// Detects and parses Safari bookmarks on macOS.
//
// Safari stores bookmarks in a binary property list (plist) file at
// ~/Library/Safari/Bookmarks.plist.  This class reads that file using
// Cocoa's NSDictionary API and produces the same ParseResult/ParseStats
// output as the Chromium and Firefox parsers.
//
// macOS TCC (Transparency, Consent and Control):
//   ~/Library/Safari/ is protected.  Avora must have Full Disk Access
//   granted in System Settings → Privacy & Security to read this file.
//   This class distinguishes between:
//     - Safari not installed (file absent)
//     - Permission denied (TCC blocks access)
//     - Readable (normal import)
//
// Safari Profiles:
//   Safari Profiles (macOS Sonoma+) share a single bookmark library.
//   Each profile may use a different Favorites folder, but the underlying
//   bookmark data is one collection.  Therefore this parser exposes a
//   single "default" profile.
//
// Reading List:
//   Safari's Reading List is stored in the same plist as bookmarks
//   (title = "com.apple.ReadingList").  Reading List entries are
//   excluded from import and counted separately in ParseStats.
//
// This class is stateless and macOS-only.
class SafariBookmarkParser {
 public:
  // Default path to Safari's bookmark plist.
  static base::FilePath DefaultBookmarksPlistPath();

  // Detects whether Safari bookmark data is available.
  // Returns a DetectedBrowser with browser = "safari" containing
  // a single profile entry.
  //
  // The profile's access_status reflects TCC restrictions:
  //   kOk              — bookmarks readable
  //   kNotFound        — Bookmarks.plist does not exist
  //   kPermissionDenied — TCC blocks access (Full Disk Access needed)
  static DetectedBrowser DetectProfile(
      const base::FilePath& bookmarks_plist_path);

  // Parses Safari's Bookmarks.plist file.
  //
  // Returns std::nullopt if the file cannot be read (missing,
  // permission denied, or corrupt).
  //
  // Excludes Reading List entries and internal proxy nodes.
  static std::optional<ParseResult> ParseBookmarksPlist(
      const base::FilePath& bookmarks_plist_path);
};

}  // namespace avora

#endif  // BUILDFLAG(IS_MAC)

#endif  // CHROME_BROWSER_AVORA_IMPORT_SAFARI_BOOKMARK_PARSER_H_
