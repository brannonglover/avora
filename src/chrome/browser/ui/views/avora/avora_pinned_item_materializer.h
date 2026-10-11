// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PINNED_ITEM_MATERIALIZER_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PINNED_ITEM_MATERIALIZER_H_

#include <string>

#include "url/gurl.h"

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

// ---------------------------------------------------------------------------
// Target drift
//
// A pinned item's persisted URL is its *target*, and browsing inside the tab
// that materializes it never rewrites that target (see PinnedItemEntry::url).
// The two are therefore free to disagree, which is the whole point: the pin
// stays the destination the user chose while its tab goes wherever they take
// it.  These are the three questions the sidebar asks about that gap -- where
// is the target, has the tab left it, and take the tab back.
// ---------------------------------------------------------------------------

// Whether |current| still shows what |target| names.  Compared loosely: a
// fragment, and an http -> https upgrade the site performed on its own, are
// not the user navigating anywhere, and a row that treated them as such
// would claim the tab had left a page it never left.  A tab with nothing
// committed yet counts as arrived -- it is still on its way to the target,
// not away from it.
bool IsAtPinnedTarget(const GURL& target, const GURL& current);

// The persisted navigation target of the pinned item |contents|
// materializes, or an invalid GURL when it materializes none: an ordinary
// tab, one whose item has since been unpinned, or one belonging to a Space
// other than the active one (PinnedItemsManager is scoped to that Space).
GURL GetPinnedTargetForTab(PinnedItemsManager* pinned_items_manager,
                           content::WebContents* contents);

// True when |contents| materializes a pinned item and has since been
// navigated off that item's target.
bool HasLeftPinnedTarget(PinnedItemsManager* pinned_items_manager,
                         content::WebContents* contents);

// Navigates |contents| back to its pinned item's target.  Deliberately an
// ordinary navigation rather than a history traversal: the pages visited in
// between stay in session history, so this is undone with Back and nothing
// the user was reading is thrown away.  A no-op when |contents| materializes
// no pinned item, or is already on its target.
void ReturnTabToPinnedTarget(PinnedItemsManager* pinned_items_manager,
                             content::WebContents* contents);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PINNED_ITEM_MATERIALIZER_H_
