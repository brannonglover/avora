// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_TAB_MARKER_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_TAB_MARKER_H_

#include "components/tabs/public/tab_interface.h"

namespace content {
class WebContents;
}

namespace avora {

// Identity marker recording that a WebContents is the page behind a link
// preview overlay rather than an ordinary tab.
//
// A preview lives in its window's TabStripModel like any other tab -- that is
// what gives it a reachable extension tab id, a window id, and the browser as
// its WebContentsDelegate -- but it must never appear in the daily list.  The
// mark is what AvoraSpaceTabFilter filters on, and it is set before insertion
// so the tab is hidden on the very first layout pass instead of flashing into
// the list and being removed once the overlay catches up.
//
// |host| is the tab the preview was opened from.  LinkPreviewTabState is the
// forward mapping and lives on the host; this is the reverse one, needed
// whenever the only thing in hand is the preview -- notably when the user
// navigates onto the hidden preview tab by model order and it has to be
// promoted back out of the overlay.
void MarkLinkPreviewTab(content::WebContents* contents, tabs::TabHandle host);

// Drops the mark, turning the preview into an ordinary tab.  Used when a
// preview is promoted -- activated directly, or opened full size.
void UnmarkLinkPreviewTab(content::WebContents* contents);

bool IsLinkPreviewTab(content::WebContents* contents);

// The tab |contents| is a preview for, or null when it is not a preview or
// its host has since gone away.
content::WebContents* GetLinkPreviewHostTab(content::WebContents* contents);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_TAB_MARKER_H_
