// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/import/safari_bookmark_parser.h"

#if BUILDFLAG(IS_MAC)

#import <Foundation/Foundation.h>

#include <string>
#include <vector>

#include "base/apple/foundation_util.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/sys_string_conversions.h"
#include "base/uuid.h"
#include "url/gurl.h"

namespace avora {

namespace {

// Safari plist node type strings.
NSString* const kTypeList = @"WebBookmarkTypeList";
NSString* const kTypeLeaf = @"WebBookmarkTypeLeaf";
NSString* const kTypeProxy = @"WebBookmarkTypeProxy";

// Safari well-known folder titles.
NSString* const kBookmarksBar = @"BookmarksBar";
NSString* const kBookmarksMenu = @"BookmarksMenu";
NSString* const kReadingList = @"com.apple.ReadingList";

std::string GenerateId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

bool IsValidUrl(const std::string& url_string) {
  if (url_string.empty()) return false;
  GURL url(url_string);
  return url.is_valid() &&
         (url.SchemeIsHTTPOrHTTPS() || url.SchemeIs("file") ||
          url.SchemeIs("chrome") || url.SchemeIs("ftp"));
}

// User-friendly titles for Safari's root folders.
std::string RootFolderTitle(NSString* safari_title) {
  if ([safari_title isEqualToString:kBookmarksBar])
    return "Favorites";
  if ([safari_title isEqualToString:kBookmarksMenu])
    return "Bookmarks Menu";
  return base::SysNSStringToUTF8(safari_title);
}

// Returns true if this is a user-content root folder we should import.
bool IsUserContentRoot(NSString* title) {
  return [title isEqualToString:kBookmarksBar] ||
         [title isEqualToString:kBookmarksMenu];
}

// Returns true if this is the Reading List container.
bool IsReadingList(NSString* title) {
  return [title isEqualToString:kReadingList];
}

// Recursively converts a Safari plist folder into ImportedItems.
void ConvertFolder(NSDictionary* folder,
                   const std::string& parent_id,
                   int& sibling_order,
                   std::vector<ImportedItem>& items,
                   ParseStats& stats) {
  NSArray* children = folder[@"Children"];
  if (!children) return;

  for (NSDictionary* child in children) {
    NSString* type = child[@"WebBookmarkType"];
    if (!type) continue;

    if ([type isEqualToString:kTypeProxy]) {
      // Internal nodes (History, Address Book, Bonjour, RSS).
      // Not user-visible bookmarks — skip without counting.
      continue;
    }

    if ([type isEqualToString:kTypeList]) {
      NSString* title = child[@"Title"];
      if (!title) title = @"";

      // Skip Reading List — intentionally excluded, not malformed.
      if (IsReadingList(title)) {
        NSArray* rl_children = child[@"Children"];
        if (rl_children) {
          stats.items_excluded += static_cast<int>([rl_children count]);
        }
        continue;
      }

      stats.folders_seen++;

      ImportedItem folder_item;
      folder_item.id = GenerateId();
      folder_item.parent_id = parent_id;
      folder_item.type = ImportedItemType::kFolder;
      folder_item.title = base::SysNSStringToUTF8(title);
      if (folder_item.title.empty()) folder_item.title = "(Untitled Folder)";
      folder_item.order = sibling_order++;
      const std::string folder_id = folder_item.id;
      items.push_back(std::move(folder_item));
      stats.folders_imported++;

      int child_order = 0;
      ConvertFolder(child, folder_id, child_order, items, stats);
      continue;
    }

    if ([type isEqualToString:kTypeLeaf]) {
      stats.links_seen++;

      NSString* url_string = child[@"URLString"];
      NSString* title =
          child[@"URIDictionary"] ? child[@"URIDictionary"][@"title"] : nil;

      std::string url = url_string ? base::SysNSStringToUTF8(url_string) : "";

      if (url.empty() || !IsValidUrl(url)) {
        stats.items_skipped++;
        continue;
      }

      ImportedItem link;
      link.id = GenerateId();
      link.parent_id = parent_id;
      link.type = ImportedItemType::kLink;
      link.title = title ? base::SysNSStringToUTF8(title) : url;
      link.url = url;
      link.order = sibling_order++;
      items.push_back(std::move(link));
      stats.links_imported++;
      continue;
    }

    // Unknown type — skip.
    stats.items_skipped++;
  }
}

// Counts user-visible bookmarks (leaf nodes) in the plist, excluding
// Reading List and proxy nodes.
int CountBookmarksInPlist(NSDictionary* root_dict) {
  int count = 0;

  // Stack-based traversal.
  NSMutableArray* stack =
      [NSMutableArray arrayWithObject:root_dict];

  while ([stack count] > 0) {
    NSDictionary* node = [stack lastObject];
    [stack removeLastObject];

    NSString* type = node[@"WebBookmarkType"];
    if (!type) continue;

    if ([type isEqualToString:kTypeProxy]) continue;

    if ([type isEqualToString:kTypeList]) {
      NSString* title = node[@"Title"];
      if (title && IsReadingList(title)) continue;

      NSArray* children = node[@"Children"];
      if (children) {
        for (NSDictionary* child in children) {
          [stack addObject:child];
        }
      }
      continue;
    }

    if ([type isEqualToString:kTypeLeaf]) {
      count++;
    }
  }

  return count;
}

}  // namespace

// static
base::FilePath SafariBookmarkParser::DefaultBookmarksPlistPath() {
  base::FilePath home = base::GetHomeDir();
  return home.Append("Library").Append("Safari").Append("Bookmarks.plist");
}

// static
DetectedBrowser SafariBookmarkParser::DetectProfile(
    const base::FilePath& bookmarks_plist_path) {
  DetectedBrowser result;
  result.browser = "safari";
  result.data_dir = bookmarks_plist_path.DirName();

  DetectedProfile profile;
  profile.directory_name = "default";
  profile.display_name = "Bookmarks";
  profile.bookmarks_path = bookmarks_plist_path;

  // Check if the file exists using stat() — this works even when
  // TCC blocks open()/read().
  if (!base::PathExists(bookmarks_plist_path)) {
    profile.access_status = ProfileAccessStatus::kNotFound;
    profile.bookmark_count = -1;
    result.profiles.push_back(std::move(profile));
    return result;
  }

  // Try to read the file.  TCC will block this with EPERM if Avora
  // doesn't have Full Disk Access.
  @autoreleasepool {
    NSURL* url = base::apple::FilePathToNSURL(bookmarks_plist_path);
    NSError* error = nil;
    NSDictionary* dict = [NSDictionary dictionaryWithContentsOfURL:url
                                                             error:&error];
    if (!dict) {
      if (error && [[error domain] isEqualToString:NSCocoaErrorDomain]) {
        NSError* underlying = [error.userInfo
            objectForKey:NSUnderlyingErrorKey];
        if (underlying &&
            [[underlying domain] isEqualToString:NSPOSIXErrorDomain] &&
            [underlying code] == EPERM) {
          profile.access_status = ProfileAccessStatus::kPermissionDenied;
        } else {
          profile.access_status = ProfileAccessStatus::kError;
        }
      } else {
        profile.access_status = ProfileAccessStatus::kError;
      }
      profile.bookmark_count = -1;
      result.profiles.push_back(std::move(profile));
      return result;
    }

    profile.access_status = ProfileAccessStatus::kOk;
    profile.bookmark_count = CountBookmarksInPlist(dict);
  }

  result.profiles.push_back(std::move(profile));
  return result;
}

// static
std::optional<ParseResult> SafariBookmarkParser::ParseBookmarksPlist(
    const base::FilePath& bookmarks_plist_path) {
  @autoreleasepool {
    NSURL* url = base::apple::FilePathToNSURL(bookmarks_plist_path);
    NSError* error = nil;
    NSDictionary* root_dict =
        [NSDictionary dictionaryWithContentsOfURL:url error:&error];
    if (!root_dict) {
      if (error) {
        LOG(WARNING) << "Failed to read Safari Bookmarks.plist: "
                     << base::SysNSStringToUTF8([error description]);
      }
      return std::nullopt;
    }

    // Verify this is a Safari bookmarks file.
    if (!root_dict[@"WebBookmarkFileVersion"]) {
      LOG(WARNING) << "Not a valid Safari Bookmarks.plist file";
      return std::nullopt;
    }

    ParseResult result;
    result.source.id = GenerateId();
    result.source.browser = "safari";
    result.source.profile_name = "Bookmarks";
    result.source.imported_at = base::Time::Now();

    // Process top-level children (BookmarksBar, BookmarksMenu, etc.).
    NSArray* top_children = root_dict[@"Children"];
    if (!top_children) {
      return result;
    }

    int root_order = 0;
    for (NSDictionary* child in top_children) {
      NSString* type = child[@"WebBookmarkType"];
      if (!type) continue;

      // Skip proxy nodes (History, Address Book, Bonjour, RSS).
      if ([type isEqualToString:kTypeProxy]) continue;

      // Skip non-list nodes at the top level.
      if (![type isEqualToString:kTypeList]) {
        // A bare leaf at the top level — unusual but handle it.
        if ([type isEqualToString:kTypeLeaf]) {
          result.stats.links_seen++;
          NSString* url_string = child[@"URLString"];
          NSString* title =
              child[@"URIDictionary"]
                  ? child[@"URIDictionary"][@"title"]
                  : nil;
          std::string url_str =
              url_string ? base::SysNSStringToUTF8(url_string) : "";
          if (!url_str.empty() && IsValidUrl(url_str)) {
            ImportedItem link;
            link.id = GenerateId();
            link.type = ImportedItemType::kLink;
            link.title = title ? base::SysNSStringToUTF8(title) : url_str;
            link.url = url_str;
            link.order = root_order++;
            result.source.items.push_back(std::move(link));
            result.stats.links_imported++;
          } else {
            result.stats.items_skipped++;
          }
        }
        continue;
      }

      NSString* title = child[@"Title"];
      if (!title) title = @"";

      // Skip Reading List — intentionally excluded, not malformed.
      if (IsReadingList(title)) {
        NSArray* rl_children = child[@"Children"];
        if (rl_children) {
          result.stats.items_excluded +=
              static_cast<int>([rl_children count]);
        }
        continue;
      }

      // Only import user-content roots.
      if (!IsUserContentRoot(title)) continue;

      // Check if this root has children.
      NSArray* children_arr = child[@"Children"];
      if (!children_arr || [children_arr count] == 0) continue;

      result.stats.folders_seen++;
      ImportedItem root_folder;
      root_folder.id = GenerateId();
      root_folder.type = ImportedItemType::kFolder;
      root_folder.title = RootFolderTitle(title);
      root_folder.order = root_order++;
      const std::string root_id = root_folder.id;
      result.source.items.push_back(std::move(root_folder));
      result.stats.folders_imported++;

      int child_order = 0;
      ConvertFolder(child, root_id, child_order, result.source.items,
                    result.stats);
    }

    return result;
  }
}

}  // namespace avora

#endif  // BUILDFLAG(IS_MAC)
