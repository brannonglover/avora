// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/import/avora_import_coordinator.h"

#include "base/containers/span.h"
#include "base/hash/sha1.h"
#include "build/build_config.h"
#include "chrome/browser/avora/avora_imported_link_store.h"
#include "chrome/browser/avora/import/chromium_bookmark_parser.h"
#include "chrome/browser/avora/import/firefox_bookmark_parser.h"
#if BUILDFLAG(IS_MAC)
#include "chrome/browser/avora/import/safari_bookmark_parser.h"
#endif

namespace avora {

// ── Browser detection ───────────────────────────────────────────────────────

// static
DetectedBrowser AvoraImportCoordinator::DetectChrome() {
  return DetectChrome(ChromiumBookmarkParser::DefaultChromeDataDir());
}

// static
DetectedBrowser AvoraImportCoordinator::DetectChrome(
    const base::FilePath& data_dir) {
  return ChromiumBookmarkParser::DetectProfiles(data_dir, "chrome");
}

// static
DetectedBrowser AvoraImportCoordinator::DetectEdge() {
  return DetectEdge(ChromiumBookmarkParser::DefaultEdgeDataDir());
}

// static
DetectedBrowser AvoraImportCoordinator::DetectEdge(
    const base::FilePath& data_dir) {
  return ChromiumBookmarkParser::DetectProfiles(data_dir, "edge");
}

// static
DetectedBrowser AvoraImportCoordinator::DetectFirefox() {
  return DetectFirefox(FirefoxBookmarkParser::DefaultFirefoxDataDir());
}

// static
DetectedBrowser AvoraImportCoordinator::DetectFirefox(
    const base::FilePath& data_dir) {
  return FirefoxBookmarkParser::DetectProfiles(data_dir);
}

#if BUILDFLAG(IS_MAC)
// static
DetectedBrowser AvoraImportCoordinator::DetectSafari() {
  return DetectSafari(SafariBookmarkParser::DefaultBookmarksPlistPath());
}

// static
DetectedBrowser AvoraImportCoordinator::DetectSafari(
    const base::FilePath& bookmarks_plist_path) {
  return SafariBookmarkParser::DetectProfile(bookmarks_plist_path);
}
#endif

// static
std::vector<DetectedBrowser> AvoraImportCoordinator::DetectAllBrowsers() {
  std::vector<DetectedBrowser> browsers;

  auto chrome = DetectChrome();
  if (!chrome.profiles.empty()) {
    browsers.push_back(std::move(chrome));
  }

  auto edge = DetectEdge();
  if (!edge.profiles.empty()) {
    browsers.push_back(std::move(edge));
  }

  auto firefox = DetectFirefox();
  if (!firefox.profiles.empty()) {
    browsers.push_back(std::move(firefox));
  }

#if BUILDFLAG(IS_MAC)
  auto safari = DetectSafari();
  if (!safari.profiles.empty()) {
    browsers.push_back(std::move(safari));
  }
#endif

  return browsers;
}

// ── Source identity ─────────────────────────────────────────────────────────

// static
std::string AvoraImportCoordinator::GenerateImportedSourceId(
    const std::string& browser,
    const std::string& source_profile_id,
    const std::string& space_id) {
  // Deterministic: same triple always produces the same ID.
  // This enables natural re-import replacement via AddSource().
  //
  // NUL separators prevent collisions between inputs whose
  // concatenation would otherwise be identical.
  const std::string origin =
      browser + '\0' + source_profile_id + '\0' + space_id;
  const auto hash = base::SHA1Hash(base::as_byte_span(origin));

  // Format the first 16 bytes of SHA-1 as a UUID v5-style string
  // (version=5, variant=RFC 4122) to match Avora's universal
  // lowercase-UUID id convention.
  uint8_t bytes[16];
  std::copy_n(hash.begin(), 16, bytes);
  bytes[6] = (bytes[6] & 0x0F) | 0x50;  // version 5
  bytes[8] = (bytes[8] & 0x3F) | 0x80;  // variant RFC 4122

  char buf[37];
  snprintf(buf, sizeof(buf),
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-"
           "%02x%02x-%02x%02x%02x%02x%02x%02x",
           bytes[0], bytes[1], bytes[2], bytes[3],
           bytes[4], bytes[5], bytes[6], bytes[7],
           bytes[8], bytes[9], bytes[10], bytes[11],
           bytes[12], bytes[13], bytes[14], bytes[15]);
  return std::string(buf, 36);
}

// ── Import ──────────────────────────────────────────────────────────────────

// static
ImportResult AvoraImportCoordinator::ImportProfile(
    const std::string& browser,
    const DetectedProfile& profile,
    const std::string& space_id,
    ImportedLinkStore* store) {
  ImportResult result;

  if (!store) {
    result.error_message = "Import store is not available.";
    return result;
  }

  if (space_id.empty()) {
    result.error_message = "No destination Space selected.";
    return result;
  }

  if (profile.bookmark_count == 0) {
    result.error_message = "This profile has no bookmarks to import.";
    return result;
  }

  if (profile.bookmark_count < 0) {
    result.error_message =
        "Could not read the bookmarks file for this profile.";
    return result;
  }

  // Parse the bookmarks using the appropriate browser parser.
  std::optional<ParseResult> parse_result;
  if (browser == "firefox") {
    parse_result = FirefoxBookmarkParser::ParsePlacesDatabase(
        profile.bookmarks_path, profile.display_name);
  } else if (browser == "safari") {
#if BUILDFLAG(IS_MAC)
    parse_result = SafariBookmarkParser::ParseBookmarksPlist(
        profile.bookmarks_path);
#else
    result.error_message = "Safari import is only supported on macOS.";
    return result;
#endif
  } else {
    parse_result = ChromiumBookmarkParser::ParseBookmarksFile(
        profile.bookmarks_path, browser, profile.display_name);
  }

  if (!parse_result) {
    result.error_message =
        "The bookmarks could not be read or the data was malformed.";
    return result;
  }

  // Use the authoritative statistics directly from the parser.
  result.stats = parse_result->stats;

  // Assign deterministic ID and destination Space.
  parse_result->source.id =
      GenerateImportedSourceId(browser, profile.directory_name, space_id);
  parse_result->source.space_id = space_id;

  // Persist.  AddSource() replaces an existing source with the same ID.
  result.source_id = store->AddSource(std::move(parse_result->source));
  result.success = true;

  return result;
}

// static
ImportResult AvoraImportCoordinator::ImportHtmlFile(
    const base::FilePath& html_path,
    const std::string& browser,
    const std::string& source_profile_id,
    const std::string& profile_display_name,
    const std::string& space_id,
    ImportedLinkStore* store) {
  ImportResult result;

  auto parse_result = HtmlBookmarkParser::ParseBookmarksHtmlFile(
      html_path, browser, profile_display_name);
  if (!parse_result) {
    result.error_message =
        "The exported bookmark file could not be read or was not recognized "
        "as a valid Netscape bookmark file.";
    return result;
  }

  result.stats = parse_result->stats;

  parse_result->source.id =
      GenerateImportedSourceId(browser, source_profile_id, space_id);
  parse_result->source.space_id = space_id;

  result.source_id = store->AddSource(std::move(parse_result->source));
  result.success = true;

  return result;
}

}  // namespace avora
