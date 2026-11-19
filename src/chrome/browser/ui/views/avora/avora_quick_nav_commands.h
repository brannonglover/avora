// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_QUICK_NAV_COMMANDS_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_QUICK_NAV_COMMANDS_H_

#include "chrome/browser/avora/avora_clear_site_data.h"

class BrowserView;
class TabStripModel;

namespace gfx {
struct VectorIcon;
}  // namespace gfx

namespace content {
class WebContents;
}  // namespace content

namespace avora {

// Running the quick-nav overlay's commands ("Clear Cache", "Clear Cookies").
// Kept out of BrowserView, whose header most of chrome/browser includes.

// The glyph for |command|, on its row in the overlay and on its toast.
const gfx::VectorIcon& SiteDataCommandIcon(SiteDataCommand command);

// The tab a command acts on: the active tab when it shows a site, or -- when
// the active tab is the blank one Cmd-T just opened for the overlay -- the tab
// it was opened from.  Null when neither shows a site.
content::WebContents* GetQuickNavCommandTarget(TabStripModel* model);

// Clears |command|'s data for the target tab's site, in that tab's Space,
// then reloads the tab and confirms with the window's toast.  When the target
// is the tab under Cmd-T's blank tab, switches back to it and closes the
// blank one first.
void RunQuickNavCommand(BrowserView* browser_view, SiteDataCommand command);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_QUICK_NAV_COMMANDS_H_
