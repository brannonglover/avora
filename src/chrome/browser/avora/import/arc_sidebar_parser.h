// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_IMPORT_ARC_SIDEBAR_PARSER_H_
#define CHROME_BROWSER_AVORA_IMPORT_ARC_SIDEBAR_PARSER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"

namespace avora {

// Parses Arc's StorableSidebar.json -- its Spaces and pinned tabs.
//
// This is deliberately NOT the Chromium bookmark path.  Arc ships a standard
// Chromium user-data directory (see ChromiumBookmarkParser::DefaultArcDataDir)
// but barely uses it: that Bookmarks file typically holds stale bookmarks
// imported from another browser years ago, not anything the user works with.
// What an Arc user actually means by "my tabs" lives here.
//
// File shape, for the reader who has to touch this next.  Two conventions in
// Arc's JSON are easy to get wrong:
//
//   1. Flat alternating lists.  "items" and "spaces" are single arrays that
//      alternate id string, then the object it names:
//        ["UUID-A", {...A...}, "UUID-B", {...B...}]
//      They are NOT arrays of objects.  Same for a space's "containerIDs",
//      which alternates a marker ("pinned" / "unpinned") with a container id.
//
//   2. Containers are themselves items.  A space's pinned container id
//      resolves to an ordinary entry in "items" whose data kind is
//      "itemContainer".  Its childrenIds give the top-level pinned entries in
//      display order, so ordering comes from walking childrenIds -- never from
//      scanning for matching parentID, which loses order.
//
// Item data kinds observed: "tab" (a pinned tab, carrying savedURL /
// savedTitle), "list" (a folder), "itemContainer" (a space's pinned or
// unpinned container).
struct ArcItem {
  // Arc's own UUID.  Used as the ImportProvenance external_id so re-importing
  // updates the record it created rather than duplicating it.
  std::string arc_id;

  // References another ArcItem's arc_id.  Empty means top level within the
  // space's pinned container.
  std::string parent_arc_id;

  std::string title;

  // Navigation target.  Empty for folders.
  std::string url;

  bool is_folder = false;

  // Position among siblings, from the parent's childrenIds order.
  int order = 0;
};

// One Arc Space and everything pinned in it.
struct ArcSpace {
  std::string arc_id;
  std::string title;

  // Flat list; tree structure via ArcItem::parent_arc_id.
  std::vector<ArcItem> items;
};

// Authoritative counts, decided at the point each node is accepted or
// discarded rather than inferred afterwards.
struct ArcParseStats {
  int spaces_seen = 0;
  int spaces_imported = 0;
  int tabs_seen = 0;
  int tabs_imported = 0;
  int folders_seen = 0;
  int folders_imported = 0;

  // Nodes discarded: unresolvable child ids, unsupported data kinds, or a
  // tab whose savedURL is missing or not a navigable scheme.
  int items_skipped = 0;
};

struct ArcParseResult {
  std::vector<ArcSpace> spaces;
  ArcParseStats stats;
};

class ArcSidebarParser {
 public:
  // ~/Library/Application Support/Arc/StorableSidebar.json on macOS; an empty
  // path elsewhere, since Arc's Windows layout is not supported yet.
  static base::FilePath DefaultSidebarPath();

  // Returns std::nullopt only when the file cannot be read or is
  // fundamentally unusable (not valid JSON, or no container holding both
  // "spaces" and "items").  Individual malformed entries are counted in
  // stats.items_skipped rather than failing the whole parse, matching how
  // ChromiumBookmarkParser treats partial damage.
  static std::optional<ArcParseResult> ParseSidebarFile(
      const base::FilePath& sidebar_path);
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_IMPORT_ARC_SIDEBAR_PARSER_H_
