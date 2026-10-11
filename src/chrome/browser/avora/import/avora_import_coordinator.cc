// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/import/avora_import_coordinator.h"

#include "base/containers/span.h"
#include "base/hash/sha1.h"
#include "build/build_config.h"
#include <map>
#include <set>

#include "base/uuid.h"
#include "chrome/browser/avora/avora_import_provenance.h"
#include "chrome/browser/avora/avora_imported_link_store.h"
#include "chrome/browser/avora/avora_pinned_folders.h"
#include "chrome/browser/avora/avora_pinned_items.h"
#include "chrome/browser/avora/avora_space.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/import/arc_sidebar_parser.h"
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

// static
DetectedBrowser AvoraImportCoordinator::DetectArc() {
  return DetectArc(ChromiumBookmarkParser::DefaultArcDataDir());
}

// static
DetectedBrowser AvoraImportCoordinator::DetectArc(
    const base::FilePath& data_dir) {
  return ChromiumBookmarkParser::DetectProfiles(data_dir, "arc");
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

  auto arc = DetectArc();
  if (!arc.profiles.empty()) {
    browsers.push_back(std::move(arc));
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


// ── Arc Spaces and pinned tabs ──────────────────────────────────────────────

namespace {

// Provenance record_type values for Arc-derived records.
constexpr char kArcSource[] = "arc";
constexpr char kRecordSpace[] = "space";
constexpr char kRecordPinnedItem[] = "pinned_item";
constexpr char kRecordPinnedFolder[] = "pinned_folder";

// The Avora record previously created for |external_id|, or empty.
std::string PreviousRecordId(ImportProvenanceStore* provenance,
                             const std::string& record_type,
                             const std::string& external_id) {
  if (!provenance) {
    return std::string();
  }
  const ImportProvenance* found =
      provenance->GetByExternalId(kArcSource, record_type, external_id);
  return found ? found->record_id : std::string();
}

void RecordProvenance(ImportProvenanceStore* provenance,
                      const std::string& record_type,
                      const std::string& record_id,
                      const std::string& external_id,
                      const std::string& batch_id) {
  if (!provenance) {
    return;
  }
  ImportProvenance record;
  record.record_type = record_type;
  record.record_id = record_id;
  record.import_source = kArcSource;
  record.external_id = external_id;
  record.import_batch_id = batch_id;
  provenance->Set(record);
}

// An existing Space whose name matches |name| exactly, or empty.
//
// Used only as a fallback when provenance has no record: a first import of an
// Arc Space called "Work" into an Avora profile that already has a "Work"
// should land in that Space rather than creating a confusing second one with
// the same name.  Provenance still wins when present, so a Space the user has
// renamed since importing is matched by id and not re-matched by name.
std::string SpaceIdByName(SpaceManager* space_manager,
                          const std::string& name) {
  if (name.empty()) {
    return std::string();
  }
  for (const Space& space : space_manager->GetSpaces()) {
    if (space.name == name) {
      return space.id;
    }
  }
  return std::string();
}

bool SpaceStillExists(SpaceManager* space_manager, const std::string& id) {
  if (id.empty()) {
    return false;
  }
  for (const Space& space : space_manager->GetSpaces()) {
    if (space.id == id) {
      return true;
    }
  }
  return false;
}

bool FolderStillExists(PinnedFoldersManager* pinned_folders,
                       const std::string& id) {
  if (id.empty()) {
    return false;
  }
  for (const PinnedFolder& folder : pinned_folders->GetFolders()) {
    if (folder.id == id) {
      return true;
    }
  }
  return false;
}

// An existing folder in the Space being imported into whose name matches
// |name| exactly, or empty.
//
// The folder-level counterpart of SpaceIdByName(), and for the same reason:
// importing Arc's "GEP" folder into a Space where the user already keeps a
// folder called "GEP" should merge into it, not sit a second identically
// named folder beside it -- which reads as the import having duplicated
// itself.  Provenance still wins when present, so a folder renamed since
// importing is matched by id and not re-matched by name.
//
// |claimed| holds the folder ids this run has already created or adopted, so
// two Arc folders sharing a name cannot both land in the same Avora folder.
std::string FolderIdByName(PinnedFoldersManager* pinned_folders,
                           const std::string& name,
                           const std::set<std::string>& claimed) {
  if (name.empty()) {
    return std::string();
  }
  for (const PinnedFolder& folder : pinned_folders->GetFolders()) {
    if (folder.name == name && !claimed.count(folder.id)) {
      return folder.id;
    }
  }
  return std::string();
}

}  // namespace

// static
ArcImportResult AvoraImportCoordinator::ImportArcSidebar(
    const base::FilePath& sidebar_path,
    SpaceManager* space_manager,
    PinnedItemsManager* pinned_items,
    PinnedFoldersManager* pinned_folders,
    ImportProvenanceStore* provenance) {
  ArcImportResult result;

  if (!space_manager || !pinned_items || !pinned_folders) {
    result.error_message = "Import services are not available.";
    return result;
  }

  auto parsed = ArcSidebarParser::ParseSidebarFile(sidebar_path);
  if (!parsed) {
    result.error_message =
        "Arc's sidebar data could not be read.  Make sure Arc is installed "
        "and has been run at least once.";
    return result;
  }
  result.stats = parsed->stats;

  if (parsed->spaces.empty()) {
    result.error_message = "No pinned tabs were found in Arc.";
    return result;
  }

  // Groups every record this run creates, so a future "undo this import" has
  // something to key on.
  const std::string batch_id = base::Uuid::GenerateRandomV4().AsLowercaseString();

  for (const ArcSpace& arc_space : parsed->spaces) {
    // Reuse the Space an earlier run created, unless the user has deleted it.
    std::string space_id =
        PreviousRecordId(provenance, kRecordSpace, arc_space.arc_id);
    const std::string name = arc_space.title.empty()
                                 ? std::string("Imported from Arc")
                                 : arc_space.title;
    if (SpaceStillExists(space_manager, space_id)) {
      result.spaces_updated++;
    } else if (!(space_id = SpaceIdByName(space_manager, name)).empty()) {
      // Adopt the user's existing same-named Space instead of duplicating it.
      result.spaces_updated++;
    } else {
      space_id = space_manager->CreateSpace(name);
      result.spaces_created++;
    }
    if (space_id.empty()) {
      continue;
    }
    RecordProvenance(provenance, kRecordSpace, space_id, arc_space.arc_id,
                     batch_id);

    // Scope both managers to this Space for the duration of its items.  This
    // is the window-local override, so it does not disturb whatever Space the
    // user is currently looking at.
    pinned_items->SetWindowActiveSpaceId(space_id);
    pinned_folders->SetWindowActiveSpaceId(space_id);

    // Arc folder id -> Avora pinned folder id, for reparenting items below.
    std::map<std::string, std::string> folder_ids;

    // Folders this run has already created or adopted in this Space; see
    // FolderIdByName().
    std::set<std::string> claimed_folder_ids;

    for (const ArcItem& item : arc_space.items) {
      if (item.is_folder) {
        const std::string folder_name =
            item.title.empty() ? std::string("Folder") : item.title;
        // Same three-step resolution the Space above uses: the folder an
        // earlier run created, else one the user already keeps under this
        // name, else a new one.
        std::string folder_id =
            PreviousRecordId(provenance, kRecordPinnedFolder, item.arc_id);
        if (FolderStillExists(pinned_folders, folder_id)) {
          result.folders_updated++;
        } else if (!(folder_id = FolderIdByName(pinned_folders, folder_name,
                                                claimed_folder_ids))
                        .empty()) {
          result.folders_updated++;
        } else {
          folder_id = pinned_folders->AddFolder(folder_name);
          if (!folder_id.empty()) {
            result.folders_created++;
          }
        }
        if (folder_id.empty()) {
          continue;
        }
        claimed_folder_ids.insert(folder_id);
        folder_ids[item.arc_id] = folder_id;
        RecordProvenance(provenance, kRecordPinnedFolder, folder_id,
                         item.arc_id, batch_id);
        continue;
      }

      std::string item_id =
          PreviousRecordId(provenance, kRecordPinnedItem, item.arc_id);
      if (item_id.empty() || !pinned_items->GetPinnedItemById(item_id)) {
        // No earlier record, or the item it named has since been unpinned.
        // Adopt the pin this Space already has for the URL before creating
        // one: AddPinnedItem() refuses a duplicate URL and returns empty, so
        // without this an Arc tab the user had already pinned was dropped
        // from the import entirely -- and, worse, never filed into the
        // folder it belongs to.
        item_id = pinned_items->GetPinnedItemIdForUrl(item.url);
        if (item_id.empty()) {
          item_id = pinned_items->AddPinnedItem(item.url, item.title);
          if (!item_id.empty()) {
            result.items_created++;
          }
        } else {
          result.items_updated++;
        }
      } else {
        result.items_updated++;
      }
      if (item_id.empty()) {
        continue;
      }
      RecordProvenance(provenance, kRecordPinnedItem, item_id, item.arc_id,
                       batch_id);

      // Arc nests pinned tabs one level deep inside folders; mirror that.
      if (!item.parent_arc_id.empty()) {
        auto folder = folder_ids.find(item.parent_arc_id);
        if (folder != folder_ids.end()) {
          pinned_folders->MoveItemToFolder(item_id, folder->second);
        }
      }
    }

    result.space_ids.push_back(space_id);
  }

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
