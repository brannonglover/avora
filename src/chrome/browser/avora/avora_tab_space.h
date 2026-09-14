// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_TAB_SPACE_H_
#define CHROME_BROWSER_AVORA_AVORA_TAB_SPACE_H_

#include <string>

namespace content {
class WebContents;
}

namespace avora {

// Associates live tabs with the Space that owns them.
//
// The tag rides along on the WebContents as user data, so no Chromium type
// needs a new field and the association survives navigation.  Tabs from every
// Space stay resident in the one TabStripModel; switching Spaces only changes
// which of them the sidebar draws, which is what makes a switch instant and
// non-destructive.

// Tags |contents| as belonging to |space_id|.  Passing an empty id clears the
// tag.
void SetTabSpaceId(content::WebContents* contents, const std::string& space_id);

// The owning Space id, or empty when the tab has never been tagged.
std::string GetTabSpaceId(content::WebContents* contents);

// Whether |contents| should be shown while |active_space_id| is active.
//
// Untagged tabs deliberately return true for any Space.  Tabs restored from a
// previous session or created before Spaces existed carry no tag, and it is far
// better for them to appear in every Space than to silently vanish.
bool TabBelongsToSpace(content::WebContents* contents,
                       const std::string& active_space_id);

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_TAB_SPACE_H_
