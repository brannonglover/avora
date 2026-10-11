// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_TABS_COMMON_AVORA_SIDE_NAV_SCROLL_H_
#define CHROME_BROWSER_UI_VIEWS_TABS_COMMON_AVORA_SIDE_NAV_SCROLL_H_

namespace views {
class ScrollView;
class View;
}  // namespace views

namespace avora {

// Returns the ScrollView that scrolls `view`, or nullptr if `view` is not
// inside one.
//
// views::ScrollView::GetScrollViewForContents() only answers for a view that
// *is* a scroll view's contents -- it looks exactly two levels up, at the
// viewport's parent.  That holds upstream, where the unpinned tab container is
// handed straight to SetContents().  It does not hold in Avora: every section
// of the side nav shares one scrolling surface, so the scroll view's contents
// is the side-nav contents view and the tab containers are its children, a
// level deeper than that helper can see.  Walking the ancestor chain instead
// answers correctly for both shapes.
views::ScrollView* FindEnclosingScrollView(const views::View* view);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_TABS_COMMON_AVORA_SIDE_NAV_SCROLL_H_
