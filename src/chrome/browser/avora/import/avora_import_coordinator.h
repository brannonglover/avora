// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_IMPORT_AVORA_IMPORT_COORDINATOR_H_
#define CHROME_BROWSER_AVORA_IMPORT_AVORA_IMPORT_COORDINATOR_H_

#include <string>
#include <vector>

#include "build/build_config.h"
#include "chrome/browser/avora/import/bookmark_import_types.h"
#include "chrome/browser/avora/import/chromium_bookmark_parser.h"
#include "chrome/browser/avora/import/firefox_bookmark_parser.h"
#include "chrome/browser/avora/import/html_bookmark_parser.h"
#if BUILDFLAG(IS_MAC)
#include "chrome/browser/avora/import/safari_bookmark_parser.h"
#endif

namespace avora {

class ImportedLinkStore;

// Result of a single import operation.
// Uses the authoritative ParseStats from the parser rather than
// inferring counts from the stored ImportedSource.
struct ImportResult {
  bool success = false;
  std::string error_message;
  ParseStats stats;             // Authoritative counts from the parser
  std::string source_id;        // ID of the created/replaced ImportedSource
};

// Orchestrates importing browser bookmarks into Avora.
//
// Responsibilities:
//   - Detect browser profiles (Chrome, Edge, Firefox, Safari)
//   - Generate deterministic source IDs for re-import matching
//   - Parse bookmarks and assign the destination Space
//   - Persist through ImportedLinkStore
//
// This class is stateless — each method is a free-standing operation.
// It does NOT read or write prefs directly; all persistence goes through
// ImportedLinkStore.
class AvoraImportCoordinator {
 public:
  // ── Browser detection ─────────────────────────────────────────────

  // Detect all importable Chrome profiles using the default Chrome data
  // directory for this platform.
  static DetectedBrowser DetectChrome();
  static DetectedBrowser DetectChrome(const base::FilePath& data_dir);

  // Detect all importable Microsoft Edge profiles using the default
  // Edge data directory for this platform.
  static DetectedBrowser DetectEdge();
  static DetectedBrowser DetectEdge(const base::FilePath& data_dir);

  // Detect all importable Firefox profiles using the default Firefox
  // data directory for this platform.
  static DetectedBrowser DetectFirefox();
  static DetectedBrowser DetectFirefox(const base::FilePath& data_dir);

#if BUILDFLAG(IS_MAC)
  // Detect Safari bookmarks.  Safari has a single shared bookmark
  // library (not per-profile), so this returns at most one profile.
  //
  // The profile's access_status indicates whether the data can be read:
  //   kOk              — readable
  //   kNotFound        — Bookmarks.plist doesn't exist
  //   kPermissionDenied — Full Disk Access required
  static DetectedBrowser DetectSafari();
  static DetectedBrowser DetectSafari(
      const base::FilePath& bookmarks_plist_path);
#endif

  // Detect all supported browsers.  Returns one DetectedBrowser per
  // browser that was found.  Browsers with no profiles are omitted.
  // On macOS, includes Safari.
  static std::vector<DetectedBrowser> DetectAllBrowsers();

  // ── Source identity ───────────────────────────────────────────────

  // Generate a deterministic source ID from the origin triple.
  // Re-importing the same browser profile into the same Space produces
  // the same ID, causing AddSource() to replace rather than duplicate.
  //
  // Intentionally importing the same profile into two different Spaces
  // produces different IDs, allowing both to coexist.
  //
  // |browser|: browser identifier, e.g. "chrome", "firefox", "edge",
  //     "safari".
  // |source_profile_id|: stable identity of the external browser
  //     profile.  For Chrome/Edge this is the profile directory name
  //     (e.g. "Default", "Profile 1").  For Firefox this is the
  //     profile directory name (e.g. "abc12345.default-release").
  //     For Safari this is "default" (single shared library).
  // |space_id|: destination Avora Space.
  //
  // Returns a lowercase UUID-format string (consistent with all other
  // Avora IDs) derived from a SHA-1 hash with version-5/variant-RFC
  // bits set.
  static std::string GenerateImportedSourceId(
      const std::string& browser,
      const std::string& source_profile_id,
      const std::string& space_id);

  // ── Import ────────────────────────────────────────────────────────

  // Import a Chromium-family browser profile's bookmarks into the
  // specified Avora Space.  Works for any browser whose Bookmarks file
  // follows the Chromium JSON format (Chrome, Edge, etc.).
  //
  // |browser| is the identifier to write into ImportedSource.browser
  // (e.g. "chrome", "edge").  It must match the value used for
  // detection so that re-import ID matching works correctly.
  //
  // On success, the ImportedSource is persisted via |store| and the
  // sidebar section updates automatically through the store's observer.
  //
  // If a source with the same origin triple already exists, it is
  // replaced (re-import).
  //
  // The caller must verify that |space_id| is still valid before
  // calling this.  If the Space has been deleted, this method returns
  // an error rather than silently importing into a nonexistent Space.
  static ImportResult ImportProfile(
      const std::string& browser,
      const DetectedProfile& profile,
      const std::string& space_id,
      ImportedLinkStore* store);

  // Import bookmarks from a Netscape Bookmark HTML file (e.g. Safari
  // "Export Bookmarks" output).  The file is parsed using the
  // HtmlBookmarkParser and the result is persisted with the given
  // browser identity and Space.
  //
  // |source_profile_id| is used for deterministic re-import matching.
  // For Safari exports this is typically "default".
  static ImportResult ImportHtmlFile(
      const base::FilePath& html_path,
      const std::string& browser,
      const std::string& source_profile_id,
      const std::string& profile_display_name,
      const std::string& space_id,
      ImportedLinkStore* store);
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_IMPORT_AVORA_IMPORT_COORDINATOR_H_
