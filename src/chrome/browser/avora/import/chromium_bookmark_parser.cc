// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/import/chromium_bookmark_parser.h"

#include <utility>

#include "base/containers/flat_map.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/uuid.h"
#include "build/build_config.h"
#include "url/gurl.h"

namespace avora {

namespace {

// ── Chromium Bookmarks JSON keys ────────────────────────────────────────────

constexpr char kRootsKey[] = "roots";
constexpr char kBookmarkBarKey[] = "bookmark_bar";
constexpr char kOtherKey[] = "other";
constexpr char kSyncedKey[] = "synced";
constexpr char kChildrenKey[] = "children";
constexpr char kNameKey[] = "name";
constexpr char kTypeKey[] = "type";
constexpr char kUrlKey[] = "url";
constexpr char kTypeUrl[] = "url";
constexpr char kTypeFolder[] = "folder";

// ── Local State profile info keys ───────────────────────────────────────────

constexpr char kLocalStateFile[] = "Local State";
constexpr char kProfileInfoCache[] = "profile";
constexpr char kInfoCache[] = "info_cache";
constexpr char kProfileNameKey[] = "name";

// Profile directory patterns used by all Chromium-based browsers.
// Guest Profile and System Profile are deliberately excluded — they
// are either empty or internal.
constexpr char kDefaultProfileDir[] = "Default";
constexpr char kProfileDirPrefix[] = "Profile ";

constexpr char kBookmarksFile[] = "Bookmarks";

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

bool IsProfileDirectory(const std::string& dir_name) {
  if (dir_name == kDefaultProfileDir) {
    return true;
  }
  if (dir_name.starts_with(kProfileDirPrefix)) {
    std::string_view suffix(dir_name);
    suffix.remove_prefix(strlen(kProfileDirPrefix));
    int number;
    return base::StringToInt(suffix, &number);
  }
  return false;
}

// Reads the Chromium Local State file and extracts profile display names.
// Returns a map of directory_name → display_name.
base::flat_map<std::string, std::string> ReadProfileDisplayNames(
    const base::FilePath& data_dir) {
  base::flat_map<std::string, std::string> names;

  const base::FilePath local_state_path =
      data_dir.AppendASCII(kLocalStateFile);

  std::string json;
  if (!base::ReadFileToString(local_state_path, &json)) {
    return names;
  }

  auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!parsed || !parsed->is_dict()) {
    return names;
  }

  const base::DictValue* profile_dict =
      parsed->GetDict().FindDict(kProfileInfoCache);
  if (!profile_dict) {
    return names;
  }
  const base::DictValue* info_cache = profile_dict->FindDict(kInfoCache);
  if (!info_cache) {
    return names;
  }

  for (const auto [dir_name, info_val] : *info_cache) {
    if (!info_val.is_dict()) {
      continue;
    }
    const std::string* display_name =
        info_val.GetDict().FindString(kProfileNameKey);
    if (display_name && !display_name->empty()) {
      names[dir_name] = *display_name;
    }
  }

  return names;
}

// Counts bookmark nodes in a Chromium root node (recursive).
int CountBookmarks(const base::DictValue& node) {
  const std::string* type = node.FindString(kTypeKey);
  if (!type) {
    return 0;
  }
  if (*type == kTypeUrl) {
    return 1;
  }
  int count = 0;
  if (const base::ListValue* children = node.FindList(kChildrenKey)) {
    for (const auto& child : *children) {
      if (child.is_dict()) {
        count += CountBookmarks(child.GetDict());
      }
    }
  }
  return count;
}

// Recursively converts a Chromium bookmark node tree into flat ImportedItems.
void ConvertNode(const base::DictValue& node,
                 const std::string& parent_id,
                 int& sibling_order,
                 std::vector<ImportedItem>& items,
                 ParseStats& stats) {
  const std::string* type = node.FindString(kTypeKey);
  if (!type) {
    stats.items_skipped++;
    return;
  }

  const std::string* name = node.FindString(kNameKey);
  const std::string title = name ? *name : std::string();

  if (*type == kTypeUrl) {
    stats.links_seen++;
    const std::string* url_str = node.FindString(kUrlKey);
    if (!url_str || !IsValidUrl(*url_str)) {
      stats.items_skipped++;
      return;
    }

    ImportedItem item;
    item.id = GenerateId();
    item.parent_id = parent_id;
    item.type = ImportedItemType::kLink;
    item.title = title;
    item.url = *url_str;
    item.order = sibling_order++;
    items.push_back(std::move(item));
    stats.links_imported++;

  } else if (*type == kTypeFolder) {
    stats.folders_seen++;
    ImportedItem folder;
    folder.id = GenerateId();
    folder.parent_id = parent_id;
    folder.type = ImportedItemType::kFolder;
    folder.title = title;
    folder.order = sibling_order++;
    const std::string folder_id = folder.id;
    items.push_back(std::move(folder));
    stats.folders_imported++;

    if (const base::ListValue* children = node.FindList(kChildrenKey)) {
      int child_order = 0;
      for (const auto& child_val : *children) {
        if (child_val.is_dict()) {
          ConvertNode(child_val.GetDict(), folder_id, child_order, items,
                      stats);
        }
      }
    }
  } else {
    stats.items_skipped++;
  }
}

// Converts one Chromium root group (e.g. "bookmark_bar") into ImportedItems.
void ConvertRootGroup(const base::DictValue& root_node,
                      const std::string& folder_title,
                      int& root_order,
                      std::vector<ImportedItem>& items,
                      ParseStats& stats) {
  const base::ListValue* children = root_node.FindList(kChildrenKey);
  if (!children || children->empty()) {
    return;
  }

  stats.folders_seen++;
  ImportedItem root_folder;
  root_folder.id = GenerateId();
  root_folder.type = ImportedItemType::kFolder;
  root_folder.title = folder_title;
  root_folder.order = root_order++;
  const std::string root_folder_id = root_folder.id;
  items.push_back(std::move(root_folder));
  stats.folders_imported++;

  int child_order = 0;
  for (const auto& child_val : *children) {
    if (child_val.is_dict()) {
      ConvertNode(child_val.GetDict(), root_folder_id, child_order, items,
                  stats);
    }
  }
}

std::optional<ParseResult> ParseBookmarksFromDict(
    const base::DictValue& root,
    const std::string& browser,
    const std::string& profile_name) {
  const base::DictValue* roots = root.FindDict(kRootsKey);
  if (!roots) {
    return std::nullopt;
  }

  ParseResult result;
  result.source.id = GenerateId();
  result.source.browser = browser;
  result.source.profile_name = profile_name;
  result.source.imported_at = base::Time::Now();

  int root_order = 0;

  if (const base::DictValue* bar = roots->FindDict(kBookmarkBarKey)) {
    const std::string* name = bar->FindString(kNameKey);
    ConvertRootGroup(*bar, name ? *name : "Bookmarks Bar", root_order,
                     result.source.items, result.stats);
  }

  if (const base::DictValue* other = roots->FindDict(kOtherKey)) {
    const std::string* name = other->FindString(kNameKey);
    ConvertRootGroup(*other, name ? *name : "Other Bookmarks", root_order,
                     result.source.items, result.stats);
  }

  if (const base::DictValue* synced = roots->FindDict(kSyncedKey)) {
    const std::string* name = synced->FindString(kNameKey);
    ConvertRootGroup(*synced, name ? *name : "Mobile Bookmarks", root_order,
                     result.source.items, result.stats);
  }

  return result;
}

}  // namespace

// ── Public API ──────────────────────────────────────────────────────────────

// static
base::FilePath ChromiumBookmarkParser::DefaultChromeDataDir() {
#if BUILDFLAG(IS_MAC)
  base::FilePath home;
  if (base::PathExists(base::FilePath("/Users"))) {
    home = base::GetHomeDir();
  }
  return home.Append("Library")
      .Append("Application Support")
      .Append("Google")
      .Append("Chrome");
#elif BUILDFLAG(IS_WIN)
  base::FilePath home = base::GetHomeDir();
  return home.Append(FILE_PATH_LITERAL("AppData"))
      .Append(FILE_PATH_LITERAL("Local"))
      .Append(FILE_PATH_LITERAL("Google"))
      .Append(FILE_PATH_LITERAL("Chrome"))
      .Append(FILE_PATH_LITERAL("User Data"));
#elif BUILDFLAG(IS_LINUX)
  base::FilePath home = base::GetHomeDir();
  return home.Append(".config").Append("google-chrome");
#else
  return base::FilePath();
#endif
}

// static
base::FilePath ChromiumBookmarkParser::DefaultEdgeDataDir() {
#if BUILDFLAG(IS_MAC)
  base::FilePath home;
  if (base::PathExists(base::FilePath("/Users"))) {
    home = base::GetHomeDir();
  }
  return home.Append("Library")
      .Append("Application Support")
      .Append("Microsoft Edge");
#elif BUILDFLAG(IS_WIN)
  base::FilePath home = base::GetHomeDir();
  return home.Append(FILE_PATH_LITERAL("AppData"))
      .Append(FILE_PATH_LITERAL("Local"))
      .Append(FILE_PATH_LITERAL("Microsoft"))
      .Append(FILE_PATH_LITERAL("Edge"))
      .Append(FILE_PATH_LITERAL("User Data"));
#elif BUILDFLAG(IS_LINUX)
  base::FilePath home = base::GetHomeDir();
  return home.Append(".config").Append("microsoft-edge");
#else
  return base::FilePath();
#endif
}

// static
DetectedBrowser ChromiumBookmarkParser::DetectProfiles(
    const base::FilePath& data_dir,
    const std::string& browser) {
  DetectedBrowser result;
  result.browser = browser;
  result.data_dir = data_dir;

  if (!base::DirectoryExists(data_dir)) {
    return result;
  }

  auto display_names = ReadProfileDisplayNames(data_dir);

  base::FileEnumerator enumerator(data_dir, /*recursive=*/false,
                                  base::FileEnumerator::DIRECTORIES);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    const std::string dir_name = path.BaseName().AsUTF8Unsafe();

    if (!IsProfileDirectory(dir_name)) {
      continue;
    }

    DetectedProfile profile;
    profile.directory_name = dir_name;
    profile.profile_path = path;
    profile.bookmarks_path = path.AppendASCII(kBookmarksFile);

    auto it = display_names.find(dir_name);
    profile.display_name =
        (it != display_names.end()) ? it->second : dir_name;

    if (base::PathExists(profile.bookmarks_path)) {
      std::string json;
      if (base::ReadFileToString(profile.bookmarks_path, &json)) {
        auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
        if (parsed && parsed->is_dict()) {
          const base::DictValue* roots =
              parsed->GetDict().FindDict(kRootsKey);
          if (roots) {
            int count = 0;
            for (const auto [key, val] : *roots) {
              if (val.is_dict()) {
                count += CountBookmarks(val.GetDict());
              }
            }
            profile.bookmark_count = count;
          } else {
            profile.bookmark_count = 0;
          }
        }
      }
    }

    result.profiles.push_back(std::move(profile));
  }

  // Sort profiles: Default first, then Profile N by number.
  std::stable_sort(
      result.profiles.begin(), result.profiles.end(),
      [](const DetectedProfile& a, const DetectedProfile& b) {
        if (a.directory_name == kDefaultProfileDir) {
          return true;
        }
        if (b.directory_name == kDefaultProfileDir) {
          return false;
        }
        return a.directory_name < b.directory_name;
      });

  return result;
}

// static
std::optional<ParseResult> ChromiumBookmarkParser::ParseBookmarksFile(
    const base::FilePath& bookmarks_path,
    const std::string& browser,
    const std::string& profile_name) {
  std::string json;
  if (!base::ReadFileToString(bookmarks_path, &json)) {
    LOG(WARNING) << "Failed to read bookmarks file: " << bookmarks_path;
    return std::nullopt;
  }
  return ParseBookmarksJson(json, browser, profile_name);
}

// static
std::optional<ParseResult> ChromiumBookmarkParser::ParseBookmarksJson(
    const std::string& json_string,
    const std::string& browser,
    const std::string& profile_name) {
  auto parsed = base::JSONReader::Read(json_string, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!parsed || !parsed->is_dict()) {
    LOG(WARNING) << "Bookmarks file is not valid JSON";
    return std::nullopt;
  }
  return ParseBookmarksFromDict(parsed->GetDict(), browser, profile_name);
}

// static
std::vector<ImportedSource> ChromiumBookmarkParser::ImportAllProfiles(
    const base::FilePath& data_dir,
    const std::string& browser) {
  std::vector<ImportedSource> results;

  DetectedBrowser detected = DetectProfiles(data_dir, browser);
  for (const auto& profile : detected.profiles) {
    if (profile.bookmark_count <= 0) {
      continue;
    }
    auto parse_result = ParseBookmarksFile(profile.bookmarks_path, browser,
                                           profile.display_name);
    if (parse_result) {
      results.push_back(std::move(parse_result->source));
    }
  }

  return results;
}

}  // namespace avora
