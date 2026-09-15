// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_IMPORTED_LINKS_H_
#define CHROME_BROWSER_AVORA_AVORA_IMPORTED_LINKS_H_

#include <string>
#include <vector>

#include "base/time/time.h"
#include "base/values.h"

namespace avora {

enum class ImportedItemType {
  kFolder = 0,
  kLink = 1,
};

// A single imported bookmark or bookmark folder from an external browser.
//
// Items form a tree via |parent_id| references within a single ImportedSource.
// Root-level items have an empty |parent_id|.
struct ImportedItem {
  std::string id;

  // References another ImportedItem's id within the same source.
  // Empty means this item is at the root of the source.
  std::string parent_id;

  ImportedItemType type = ImportedItemType::kLink;

  std::string title;

  // Navigation URL.  Empty for folders.
  std::string url;

  // Sort position among siblings sharing the same parent_id.
  int order = 0;

  base::DictValue ToDict() const;
  static ImportedItem FromDict(const base::DictValue& dict);
};

// A single import operation — all bookmarks from one browser profile,
// belonging to a specific Avora Space.
struct ImportedSource {
  std::string id;

  // The Avora Space this import belongs to.  The Imported section in a
  // window only shows sources whose space_id matches that window's
  // WindowSpaceState::active_space_id().
  std::string space_id;

  // Source browser identifier, e.g. "chrome", "firefox", "edge", "safari".
  // Stored as a free-form string so new browsers can be added without
  // modifying the data model.
  std::string browser;

  // Human-readable name of the source browser profile, e.g. "Default",
  // "Work".  Displayed as a sub-header under the browser name.
  std::string profile_name;

  base::Time imported_at;

  std::vector<ImportedItem> items;

  base::DictValue ToDict() const;
  static ImportedSource FromDict(const base::DictValue& dict);
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_IMPORTED_LINKS_H_
