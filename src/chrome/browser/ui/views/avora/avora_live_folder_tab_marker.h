// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LIVE_FOLDER_TAB_MARKER_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LIVE_FOLDER_TAB_MARKER_H_

#include <optional>
#include <string>

#include "base/callback_list.h"
#include "base/functional/callback_forward.h"

namespace content {
class WebContents;
}

namespace avora {

// Which Live Folder row a tab belongs to.  The Space is part of the key
// because folder ids are fixed per provider: every Space's GitHub folder is
// called the same thing.
struct LiveFolderTabKey {
  std::string space_id;
  std::string folder_id;
  std::string external_id;

  bool operator==(const LiveFolderTabKey&) const = default;
};

// Identity marker recording that a WebContents is the tab behind a Live
// Folder row -- a pull request opened from the GitHub folder.  The tab is
// represented by that row, the way a pinned item's tab is represented by its
// pinned row (avora_pinned_item_tab_marker.h), so AvoraSpaceTabFilter keeps
// it out of the daily list and out of daily tab sync.
//
// Unlike the pinned marker, marking and unmarking announce themselves (see
// AddLiveFolderTabMarkChangedCallback()): the folder adopts and releases tabs
// on its own schedule, when sync results arrive, not from inside a tab strip
// event that the daily list would already re-run its rules for.
void MarkLiveFolderTab(content::WebContents* contents,
                       const LiveFolderTabKey& key);
void UnmarkLiveFolderTab(content::WebContents* contents);
bool IsLiveFolderTab(content::WebContents* contents);
std::optional<LiveFolderTabKey> GetLiveFolderTabKey(
    content::WebContents* contents);

// Fires after a tab gains or loses its Live Folder mark, with that tab as the
// argument.  A discarded tab carrying its mark to the replacement contents
// does not count: nothing about what the tab represents changed.
base::CallbackListSubscription AddLiveFolderTabMarkChangedCallback(
    base::RepeatingCallback<void(content::WebContents*)> callback);

// Moves |old_contents|' mark, if any, onto |new_contents| without announcing
// it.  For a discard, which swaps the WebContents behind a tab.
void TransferLiveFolderTabMark(content::WebContents* old_contents,
                               content::WebContents* new_contents);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LIVE_FOLDER_TAB_MARKER_H_
