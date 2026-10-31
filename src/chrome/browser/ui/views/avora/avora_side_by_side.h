// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SIDE_BY_SIDE_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SIDE_BY_SIDE_H_

#include <vector>

class TabStripModel;

namespace split_tabs {
enum class SplitTabCreatedSource;
}  // namespace split_tabs

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace avora {

// Side by side is Chromium's split tabs (TabStripModel, MultiContentsView,
// SplitTabView) offered on the daily list: a pair is one row there, and two
// panes in the main window.
//
// A pair is an arrangement of this window, not a property of the tabs: each
// window holds its own copy of a daily tab, and the other windows' copies stay
// ordinary tabs.  AvoraSpaceTabFilter keeps both tabs of a pair in one Space.

// Whether |tab| can join a pair: a daily tab that isn't in one already.  A
// Favorite's, pinned item's, Live Folder's or link preview's tab is represented
// somewhere other than the daily list, so it has no row to combine.
bool CanPutTabSideBySide(tabs::TabInterface* tab);

// Whether |a| and |b| can be a pair, which takes two such tabs in one Space.
bool CanPutTabsSideBySide(tabs::TabInterface* a, tabs::TabInterface* b);

// The tabs |tab| could pair with, the active one first and then in list order.
std::vector<tabs::TabInterface*> GetSideBySidePartners(
    const TabStripModel& model,
    tabs::TabInterface* tab);

// Makes |anchor| and |incoming| a pair, |anchor| on the left, and puts the pair
// on screen.  Does nothing unless CanPutTabsSideBySide().
void PutTabsSideBySide(TabStripModel& model,
                       tabs::TabInterface* anchor,
                       tabs::TabInterface* incoming,
                       split_tabs::SplitTabCreatedSource source);

// Inserting a tab between the two tabs of a pair silently dissolves it, so
// anything placing a tab next to another -- which may be a pair's left tab --
// passes the index through here.  Returns |index|, or the index just past the
// pair when |index| falls inside one.
int InsertionIndexOutsideSplit(const TabStripModel& model, int index);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SIDE_BY_SIDE_H_
