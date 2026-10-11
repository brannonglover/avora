// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/tabs/common/avora_side_nav_scroll.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/view.h"

namespace avora {
namespace {

using AvoraSideNavScrollTest = testing::Test;

TEST_F(AvoraSideNavScrollTest, FindsScrollViewFromItsOwnContents) {
  views::ScrollView scroll_view;
  views::View* contents = scroll_view.SetContents(
      std::make_unique<views::View>());

  EXPECT_EQ(&scroll_view, FindEnclosingScrollView(contents));
}

// The shape Avora's side nav actually has: every section, the daily tab list
// among them, is a child of the one contents view rather than being the
// contents itself.  views::ScrollView::GetScrollViewForContents() cannot see
// past that extra level, which is why the tab containers ask this helper
// instead -- a null answer there aborts the browser on the first drag update
// (DraggedTabsContainer::InitializeDragState CHECKs the scroll view).
TEST_F(AvoraSideNavScrollTest, FindsScrollViewFromASectionInsideTheContents) {
  views::ScrollView scroll_view;
  views::View* contents = scroll_view.SetContents(
      std::make_unique<views::View>());
  views::View* section = contents->AddChildView(
      std::make_unique<views::View>());
  views::View* row = section->AddChildView(std::make_unique<views::View>());

  EXPECT_EQ(&scroll_view, FindEnclosingScrollView(section));
  EXPECT_EQ(&scroll_view, FindEnclosingScrollView(row));
}

TEST_F(AvoraSideNavScrollTest, ReturnsNullOutsideAnyScrollView) {
  views::View parent;
  views::View* child = parent.AddChildView(std::make_unique<views::View>());

  EXPECT_EQ(nullptr, FindEnclosingScrollView(&parent));
  EXPECT_EQ(nullptr, FindEnclosingScrollView(child));
  EXPECT_EQ(nullptr, FindEnclosingScrollView(nullptr));
}

}  // namespace
}  // namespace avora
