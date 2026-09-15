// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_IMPORT_HTML_BOOKMARK_PARSER_H_
#define CHROME_BROWSER_AVORA_IMPORT_HTML_BOOKMARK_PARSER_H_

#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "chrome/browser/avora/import/chromium_bookmark_parser.h"

namespace avora {

// Parses Netscape Bookmark HTML files (the standard format exported
// by Safari, Chrome, Firefox, and other browsers) into Avora's
// ImportedSource/ParseResult model.
//
// This is a lightweight Avora-native parser that handles the core
// Netscape bookmark tags (<H3> folders, <A> links, <DL> nesting)
// without pulling in Chromium's heavy content-layer dependencies.
//
// Primary use case: Safari "Export Bookmarks" → .html file → import
// into Avora when Full Disk Access is not available.
class HtmlBookmarkParser {
 public:
  // Parses a Netscape bookmark HTML file from disk.
  //
  // |browser| and |profile_name| are written into the returned
  // ImportedSource metadata.  The source has an empty space_id —
  // the caller assigns one before persisting.
  //
  // Returns std::nullopt if the file cannot be read or contains
  // no recognizable bookmark content.
  static std::optional<ParseResult> ParseBookmarksHtmlFile(
      const base::FilePath& html_path,
      const std::string& browser,
      const std::string& profile_name);

  // Parses from a string rather than a file.  Useful for testing.
  static std::optional<ParseResult> ParseBookmarksHtml(
      const std::string& html,
      const std::string& browser,
      const std::string& profile_name);
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_IMPORT_HTML_BOOKMARK_PARSER_H_
