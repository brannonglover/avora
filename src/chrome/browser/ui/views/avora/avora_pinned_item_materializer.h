// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PINNED_ITEM_MATERIALIZER_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PINNED_ITEM_MATERIALIZER_H_

#include <string>

class TabStripModel;

namespace content {
class WebContents;
}  // namespace content

namespace avora {

class PinnedFoldersManager;
class PinnedItemsManager;

// Returns the index of a live tab in |tab_strip| that already materializes
// |pinned_item_id| and is visible in |active_space_id|, or
// TabStripModel::kNoTab if none exists.
//
// Deliberately scoped to a single TabStripModel: every Avora window owns its
// own model, so a pinned item can be independently materialized in several
// windows at once, and a lookup against one window's model can never resolve
// to (or be used to activate) a tab living in another window's.
//
// This is the "does a live tab already exist" half of materialize-or-
// activate; the caller is responsible for creating a new tab and marking it
// with MarkPinnedItemTab() (avora_pinned_item_tab_marker.h) when this
// returns TabStripModel::kNoTab.
int FindMaterializedPinnedItemTab(TabStripModel* tab_strip,
                                  const std::string& pinned_item_id,
                                  const std::string& active_space_id);

// Makes Avora's native pin gesture (see tab_drag_handler.cc) create real
// kPinned persistence, not just flip TabStripModel's pinned bit.
//
// Pins |contents| in |tab_strip| if not already pinned there, then ensures
// it has a backing kPinned item in |pinned_items_manager|: reuses an
// existing item for its URL when one exists (so pinning a tab that matches
// an already-pinned URL never creates a duplicate record), otherwise
// creates one, then marks |contents| with it via MarkPinnedItemTab(). A
// no-op for the persistence half when |contents| already carries a marker
// -- repinning an already-backed tab (e.g. one BackfillNativePinnedTabs()
// or a previous drag already claimed) must never create a second record.
void PinAndCreatePinnedItem(TabStripModel* tab_strip,
                            PinnedItemsManager* pinned_items_manager,
                            content::WebContents* contents);

// The inverse: unpins |contents| in |tab_strip| if pinned there, and
// independently removes any kPinned persistence it carries -- the
// SidebarItemStore record via |pinned_items_manager|, its membership in
// whatever folder (if any) via |pinned_folders_manager|, and the WebContents
// marker -- so an unpinned item does not silently return after a restart and
// never leaves a dangling id behind in a folder. |pinned_folders_manager|
// may be null (e.g. a call site with no folder concept in scope); the
// folder-membership scrub is simply skipped in that case. The other halves
// are independent because a tab can carry either, both, or (after this
// call) neither. Never closes or navigates |contents|: the live page stays
// open as an ordinary tab, which is the whole point of Pinned being
// persistence-backed rather than a property of the tab itself.
void UnpinAndRemovePinnedItem(TabStripModel* tab_strip,
                              PinnedItemsManager* pinned_items_manager,
                              PinnedFoldersManager* pinned_folders_manager,
                              content::WebContents* contents);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PINNED_ITEM_MATERIALIZER_H_
