// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/import/arc_sidebar_parser.h"

#include <map>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "build/build_config.h"
#include "url/gurl.h"

namespace avora {

namespace {

constexpr char kSidebarKey[] = "sidebar";
constexpr char kContainersKey[] = "containers";
constexpr char kItemsKey[] = "items";
constexpr char kSpacesKey[] = "spaces";
constexpr char kContainerIDsKey[] = "containerIDs";
constexpr char kChildrenIdsKey[] = "childrenIds";
constexpr char kDataKey[] = "data";
constexpr char kTitleKey[] = "title";
constexpr char kIdKey[] = "id";

// Item data kinds.
constexpr char kKindTab[] = "tab";
constexpr char kKindList[] = "list";

// Tab payload fields.
constexpr char kSavedUrlKey[] = "savedURL";
constexpr char kSavedTitleKey[] = "savedTitle";

// Marker naming the pinned container inside a space's containerIDs.
constexpr char kPinnedMarker[] = "pinned";

bool IsValidUrl(const std::string& url_string) {
  if (url_string.empty()) {
    return false;
  }
  GURL url(url_string);
  return url.is_valid() &&
         (url.SchemeIsHTTPOrHTTPS() || url.SchemeIs("file") ||
          url.SchemeIs("chrome") || url.SchemeIs("ftp"));
}

// Arc stores id->object as one flat array alternating the id string and the
// object it names.  Collapses that into a lookup keyed by id.
std::map<std::string, const base::DictValue*> ResolveAlternatingList(
    const base::ListValue& list) {
  std::map<std::string, const base::DictValue*> resolved;
  for (size_t i = 0; i + 1 < list.size(); ++i) {
    const std::string* id = list[i].GetIfString();
    const base::DictValue* obj = list[i + 1].GetIfDict();
    if (id && obj) {
      resolved[*id] = obj;
    }
  }
  return resolved;
}

// Space objects, in the same alternating layout but keeping only the objects.
std::vector<const base::DictValue*> ResolveSpaceObjects(
    const base::ListValue& list) {
  std::vector<const base::DictValue*> spaces;
  for (size_t i = 0; i + 1 < list.size(); ++i) {
    if (list[i].GetIfString() && list[i + 1].GetIfDict()) {
      spaces.push_back(list[i + 1].GetIfDict());
    }
  }
  return spaces;
}

// A space's containerIDs alternates marker -> container id.  Returns the id
// following |marker|, or empty when the marker is absent.
std::string FindContainerId(const base::DictValue& space,
                            const std::string& marker) {
  const base::ListValue* ids = space.FindList(kContainerIDsKey);
  if (!ids) {
    return std::string();
  }
  for (size_t i = 0; i + 1 < ids->size(); ++i) {
    const std::string* key = (*ids)[i].GetIfString();
    const std::string* value = (*ids)[i + 1].GetIfString();
    if (key && value && *key == marker) {
      return *value;
    }
  }
  return std::string();
}

// The data kind: "tab", "list" or "itemContainer".
std::string ItemKind(const base::DictValue& item) {
  const base::DictValue* data = item.FindDict(kDataKey);
  if (!data || data->empty()) {
    return std::string();
  }
  return data->begin()->first;
}

// Walks |parent_id|'s childrenIds in order, appending to |out|.
//
// |depth| guards against a childrenIds cycle in a corrupt file; Arc nests
// only one level today, so the bound is generous rather than tight.
void WalkChildren(const std::map<std::string, const base::DictValue*>& items,
                  const std::string& parent_id,
                  const std::string& parent_arc_id,
                  int depth,
                  std::vector<ArcItem>& out,
                  ArcParseStats& stats) {
  constexpr int kMaxDepth = 16;
  if (depth > kMaxDepth) {
    return;
  }

  auto parent_it = items.find(parent_id);
  if (parent_it == items.end()) {
    return;
  }
  const base::ListValue* children = parent_it->second->FindList(kChildrenIdsKey);
  if (!children) {
    return;
  }

  int order = 0;
  for (const auto& child_value : *children) {
    const std::string* child_id = child_value.GetIfString();
    if (!child_id) {
      stats.items_skipped++;
      continue;
    }
    auto child_it = items.find(*child_id);
    if (child_it == items.end()) {
      stats.items_skipped++;
      continue;
    }
    const base::DictValue& child = *child_it->second;

    const std::string kind = ItemKind(child);
    const std::string* title = child.FindString(kTitleKey);

    if (kind == kKindTab) {
      stats.tabs_seen++;
      const base::DictValue* data = child.FindDict(kDataKey);
      const base::DictValue* tab = data ? data->FindDict(kKindTab) : nullptr;
      const std::string* saved_url =
          tab ? tab->FindString(kSavedUrlKey) : nullptr;
      if (!saved_url || !IsValidUrl(*saved_url)) {
        stats.items_skipped++;
        continue;
      }

      // Arc keeps a user-editable title alongside the page's own title; prefer
      // the user's, since that is what they see in the sidebar.
      const std::string* saved_title =
          tab ? tab->FindString(kSavedTitleKey) : nullptr;
      std::string resolved_title;
      if (title && !title->empty()) {
        resolved_title = *title;
      } else if (saved_title && !saved_title->empty()) {
        resolved_title = *saved_title;
      }

      ArcItem item;
      item.arc_id = *child_id;
      item.parent_arc_id = parent_arc_id;
      item.title = resolved_title;
      item.url = *saved_url;
      item.is_folder = false;
      item.order = order++;
      out.push_back(std::move(item));
      stats.tabs_imported++;

    } else if (kind == kKindList) {
      stats.folders_seen++;
      ArcItem folder;
      folder.arc_id = *child_id;
      folder.parent_arc_id = parent_arc_id;
      folder.title = title ? *title : std::string();
      folder.is_folder = true;
      folder.order = order++;
      out.push_back(std::move(folder));
      stats.folders_imported++;

      WalkChildren(items, *child_id, *child_id, depth + 1, out, stats);

    } else {
      // Nested containers and any kind Arc adds later.
      stats.items_skipped++;
    }
  }
}

}  // namespace

// static
base::FilePath ArcSidebarParser::DefaultSidebarPath() {
#if BUILDFLAG(IS_MAC)
  base::FilePath home;
  if (!base::PathService::Get(base::DIR_HOME, &home)) {
    return base::FilePath();
  }
  return home.Append("Library")
      .Append("Application Support")
      .Append("Arc")
      .Append("StorableSidebar.json");
#else
  return base::FilePath();
#endif
}

// static
std::optional<ArcParseResult> ArcSidebarParser::ParseSidebarFile(
    const base::FilePath& sidebar_path) {
  if (sidebar_path.empty()) {
    return std::nullopt;
  }

  std::string json;
  if (!base::ReadFileToString(sidebar_path, &json)) {
    return std::nullopt;
  }

  auto parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }

  const base::DictValue* sidebar = parsed->GetDict().FindDict(kSidebarKey);
  if (!sidebar) {
    return std::nullopt;
  }
  const base::ListValue* containers = sidebar->FindList(kContainersKey);
  if (!containers) {
    return std::nullopt;
  }

  // Arc writes several containers and only one carries the sidebar proper;
  // search for it rather than indexing, so an added container doesn't break
  // the import.
  const base::DictValue* sidebar_container = nullptr;
  for (const auto& value : *containers) {
    const base::DictValue* dict = value.GetIfDict();
    if (dict && dict->FindList(kItemsKey) && dict->FindList(kSpacesKey)) {
      sidebar_container = dict;
      break;
    }
  }
  if (!sidebar_container) {
    return std::nullopt;
  }

  const auto items =
      ResolveAlternatingList(*sidebar_container->FindList(kItemsKey));
  const auto space_objects =
      ResolveSpaceObjects(*sidebar_container->FindList(kSpacesKey));

  ArcParseResult result;
  for (const base::DictValue* space_dict : space_objects) {
    result.stats.spaces_seen++;

    const std::string* space_id = space_dict->FindString(kIdKey);
    if (!space_id || space_id->empty()) {
      result.stats.items_skipped++;
      continue;
    }

    const std::string pinned_container = FindContainerId(*space_dict, kPinnedMarker);
    if (pinned_container.empty()) {
      // A space with no pinned container has nothing to import.
      continue;
    }

    ArcSpace space;
    space.arc_id = *space_id;
    const std::string* title = space_dict->FindString(kTitleKey);
    space.title = title ? *title : std::string();

    WalkChildren(items, pinned_container, /*parent_arc_id=*/std::string(),
                 /*depth=*/0, space.items, result.stats);

    if (space.items.empty()) {
      continue;
    }

    result.spaces.push_back(std::move(space));
    result.stats.spaces_imported++;
  }

  return result;
}

}  // namespace avora
