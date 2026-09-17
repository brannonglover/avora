// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PINNED_ITEM_TAB_MARKER_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PINNED_ITEM_TAB_MARKER_H_

#include <string>

namespace content {
class WebContents;
}

namespace avora {

// Identity marker recording that a WebContents is the runtime backing for a
// pinned item (PinnedItemsManager / SidebarItemType::kPinned).  Mirrors
// avora_favorite_tab_marker.h's role for Favorites: the mark is valid the
// moment the tab is created, before any navigation commits, and is what lets
// a click on the same pinned item find and activate this tab instead of
// opening a second one.
//
// Unlike a Favorite, a pinned item never touches TabStripModel's own pinned
// bit -- this marker is the entire "is this tab materializing a pinned item"
// signal, independent of whether the tab happens to also be natively pinned.
void MarkPinnedItemTab(content::WebContents* contents,
                      const std::string& pinned_item_id);
void UnmarkPinnedItemTab(content::WebContents* contents);
bool IsPinnedItemTab(content::WebContents* contents);
std::string GetPinnedItemIdForTab(content::WebContents* contents);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PINNED_ITEM_TAB_MARKER_H_
