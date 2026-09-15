// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_IMPORT_BOOKMARK_IMPORT_TYPES_H_
#define CHROME_BROWSER_AVORA_IMPORT_BOOKMARK_IMPORT_TYPES_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"

namespace avora {

// Access status for a detected profile's bookmark data.
enum class ProfileAccessStatus {
  kOk,                // Data is readable.
  kNotFound,          // Profile or bookmark file does not exist.
  kPermissionDenied,  // OS-level permission blocks access (e.g. macOS TCC).
  kError,             // Other read/parse failure.
};

// A detected browser profile on disk that may contain importable data.
struct DetectedProfile {
  // Filesystem directory name, e.g. "Default", "Profile 2" for
  // Chromium-family browsers, or "abc12345.default-release" for Firefox.
  // Used as the stable source_profile_id for import identity.
  std::string directory_name;

  // Human-readable name from the browser's metadata file,
  // e.g. "Personal", "WarnerMedia" (Chrome/Edge Local State)
  // or "default-release", "Work" (Firefox profiles.ini Name field).
  // Falls back to |directory_name| when metadata is unavailable.
  std::string display_name;

  // Absolute path to the profile directory.
  base::FilePath profile_path;

  // Absolute path to the bookmarks data file (may not exist).
  // For Chromium-family browsers this is the Bookmarks JSON file.
  // For Firefox this is the places.sqlite database.
  // For Safari this is ~/Library/Safari/Bookmarks.plist.
  base::FilePath bookmarks_path;

  // Number of bookmarks detected.  -1 means the file was not scanned
  // (e.g. missing or unreadable); 0 means the file exists but is empty.
  int bookmark_count = -1;

  // Access status for this profile's bookmark data.
  // Defaults to kOk; set to a specific value by detection code when
  // data cannot be read (e.g. kPermissionDenied for macOS TCC).
  ProfileAccessStatus access_status = ProfileAccessStatus::kOk;
};

// Result of detecting all importable profiles for a single browser.
struct DetectedBrowser {
  // Browser identifier matching ImportedSource::browser,
  // e.g. "chrome", "edge", "firefox", "safari".
  std::string browser;

  // Root data directory for this browser,
  // e.g. ~/Library/Application Support/Google/Chrome
  base::FilePath data_dir;

  std::vector<DetectedProfile> profiles;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_IMPORT_BOOKMARK_IMPORT_TYPES_H_
