// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_FAVORITE_TAB_MARKER_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_FAVORITE_TAB_MARKER_H_

#include <string>

namespace content {
class WebContents;
}

namespace avora {

// Identity marker recording that a WebContents is the runtime backing for
// a favorite.  Unlike a URL comparison, the mark is valid the moment the
// tab is created — before any navigation commits — so views can filter the
// tab out on the very first layout pass instead of showing it for a frame
// and then removing it once the URL settles.
// `was_pinned` records whether the tab was already pinned by the user
// before the favorite adopted it, so releasing the favorite can restore
// the tab's original state instead of unpinning something the user pinned.
void MarkFavoriteTab(content::WebContents* contents,
                     const std::string& favorite_id,
                     bool was_pinned);
void UnmarkFavoriteTab(content::WebContents* contents);
bool IsFavoriteTab(content::WebContents* contents);
std::string GetFavoriteIdForTab(content::WebContents* contents);
bool WasPinnedBeforeFavorite(content::WebContents* contents);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_FAVORITE_TAB_MARKER_H_
