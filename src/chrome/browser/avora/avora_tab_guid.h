// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_TAB_GUID_H_
#define CHROME_BROWSER_AVORA_AVORA_TAB_GUID_H_

#include <string>

namespace content {
class NavigationController;
class NavigationEntry;
class WebContents;
}

namespace avora {

// Key written into SerializedNavigationEntry::extended_info_map for session
// restore.  Must stay stable across releases once shipped.
inline constexpr char kTabGuidExtendedInfoKey[] = "avora_tab_guid";

// Registers the session ExtendedInfoHandler.  Called from
// ChromeSerializedNavigationDriver's constructor during browser startup.
void RegisterTabGuidSessionHandler();

// GUID stored on WebContents user data, or empty if Avora has not yet claimed
// the tab.
std::string GetTabGuid(content::WebContents* contents);

// Assigns |guid| to |contents| and mirrors it onto every persisted entry.
void SetTabGuid(content::WebContents* contents, const std::string& guid);

// Returns the tab's GUID, generating and persisting one on first claim.
std::string GetOrCreateTabGuid(content::WebContents* contents);

// Reads a GUID previously restored onto a navigation entry.
std::string GetTabGuidFromNavigationEntry(content::NavigationEntry* entry);

// Writes |guid| onto a single navigation entry for session serialization.
void RestoreTabGuidOnNavigationEntry(content::NavigationEntry* entry,
                                     const std::string& guid);

// Scans |controller| for any entry carrying an Avora tab GUID.  Prefers the
// selected entry, then walks outward so a partially-synced strip still resolves.
std::string FindTabGuidInController(content::NavigationController* controller);

// Copies the GUID from restored entries onto |contents| when session restore
// has populated entries before WebContents user data exists.
void AdoptTabGuidFromController(content::WebContents* contents);

// Mirrors the WebContents GUID onto every navigation entry so session save
// round-trips regardless of which entry falls inside the persist window.
void SyncTabGuidToAllEntries(content::WebContents* contents);

// Preserves tab identity when discard swaps in a fresh WebContents.
void TransferTabGuid(content::WebContents* old_contents,
                     content::WebContents* new_contents);

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_TAB_GUID_H_
