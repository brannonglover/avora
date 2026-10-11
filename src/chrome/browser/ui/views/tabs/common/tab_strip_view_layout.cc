// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/common/tab_strip_view_layout.h"

#include <algorithm>

#include "base/logging.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/tabs/tab_style.h"
#include "chrome/browser/ui/views/tabs/common/pinned_tab_container_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/common/unpinned_tab_container_view.h"
#include "chrome/browser/ui/views/tabs/horizontal/tab_scroll_button_container.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/separator.h"
#include "ui/views/view_utils.h"

namespace {

// The pinned container main axis size should not be larger than half the
// available space unless the unpinned container will not fill that space.
// When pinned tabs are present, the size must be at least as large as the min
// size.
int CalculatePinnedContainerMainAxisSize(int pinned_preferred_size,
                                         int unpinned_preferred_size,
                                         int available_size,
                                         int min_pinned_size) {
  if (pinned_preferred_size == 0) {
    return 0;
  }
  const int tab_overlap = TabStyle::Get()->GetTabOverlap();
  const int effective_unpinned_size =
      unpinned_preferred_size > 0
          ? std::max(0, unpinned_preferred_size - tab_overlap)
          : 0;
  const int target_size = std::min(
      pinned_preferred_size,
      std::max(available_size - effective_unpinned_size, available_size / 2));
  return std::max(target_size, min_pinned_size);
}

}  // namespace

TabStripViewLayout::TabStripViewLayout(TabStripOrientation orientation)
    : orientation_(orientation) {}

TabStripViewLayout::~TabStripViewLayout() = default;

views::ProposedLayout TabStripViewLayout::CalculateProposedLayout(
    const views::SizeBounds& size_bounds) const {
  const TabStripView* tab_strip_view =
      views::AsViewClass<TabStripView>(host_view());
  if (!tab_strip_view) {
    return views::ProposedLayout();
  }

  if (orientation_ == TabStripOrientation::kHorizontal) {
    return CalculateHorizontalLayout(tab_strip_view, size_bounds);
  }
  return CalculateVerticalLayout(tab_strip_view, size_bounds);
}

views::ProposedLayout TabStripViewLayout::CalculateHorizontalLayout(
    const TabStripView* tab_strip_view,
    const views::SizeBounds& size_bounds) const {
  views::ProposedLayout layouts;
  views::ScrollView* pinned_tabs_scroll_view =
      tab_strip_view->pinned_tabs_scroll_view();
  views::ScrollView* unpinned_tabs_scroll_view =
      tab_strip_view->unpinned_tabs_scroll_view();
  views::Separator* tabs_separator = tab_strip_view->GetTabsSeparator();
  TabScrollButtonContainer* scroll_button_container =
      tab_strip_view->GetScrollButtonContainer();
  const int scroll_button_container_preferred_width =
      scroll_button_container
          ? scroll_button_container->GetPreferredSize(size_bounds).width()
          : 0;

  const int tab_overlap = TabStyle::Get()->GetTabOverlap();
  int x = 0;
  const int container_height = TabStyle::Get()->GetStandardHeight();

  const auto* unpinned_container = tab_strip_view->GetUnpinnedTabsContainer();
  const int pinned_preferred_width =
      pinned_tabs_scroll_view->GetPreferredSize(size_bounds).width();
  // Use unconstrained preferred size so the layout accounts for the total
  // desired width of unpinned tabs and groups. The unpinned container may not
  // be set yet so fallback to 0 if it doesn't exist.
  const int unpinned_preferred_width =
      unpinned_container ? unpinned_container->GetUnconstrainedPreferredWidth()
                         : 0;

  const views::SizeBound available_width = size_bounds.width();

  // Place the pinned container.
  int pinned_width = pinned_preferred_width;
  if (available_width.is_bounded()) {
    int min_pinned_width = 0;
    if (const auto* pinned_container =
            tab_strip_view->GetPinnedTabsContainer()) {
      min_pinned_width = pinned_container->GetMinimumSize().width();
    }
    pinned_width = CalculatePinnedContainerMainAxisSize(
        pinned_preferred_width, unpinned_preferred_width,
        available_width.value(), min_pinned_width);
  }

  gfx::Rect pinned_bounds(x, 0, pinned_width, container_height);
  layouts.child_layouts.emplace_back(pinned_tabs_scroll_view,
                                     pinned_tabs_scroll_view->GetVisible(),
                                     pinned_bounds);
  const bool has_unpinned = unpinned_preferred_width > 0;

  if (pinned_width > 0) {
    // Unpinned container overlaps with the last pinned tab by tab_overlap.
    x += has_unpinned ? std::max(0, pinned_width - tab_overlap) : pinned_width;
  }

  // The tabs separator isn't visible for the horizontal orientation.
  layouts.child_layouts.emplace_back(tabs_separator, false, gfx::Rect());

  // Place the unpinned container.
  int unpinned_width = unpinned_preferred_width;
  bool show_scroll_buttons = false;

  if (available_width.is_bounded()) {
    int available_unpinned_width = std::max(available_width.value() - x, 0);

    // If placing the unpinned scroll view into the available space causes an
    // overflow, reserve space for the scroll buttons.
    const bool will_overflow_without_scroll_buttons =
        unpinned_container &&
        available_unpinned_width < unpinned_container->GetMinimumSize().width();
    const bool is_scroll_buttons_pinned =
        tab_strip_view->IsTabScrollButtonsPinned();
    if (has_unpinned && will_overflow_without_scroll_buttons &&
        is_scroll_buttons_pinned) {
      available_unpinned_width = std::max(
          available_unpinned_width - scroll_button_container_preferred_width,
          0);
      show_scroll_buttons = true;
    }

    tab_strip_view->SetAvailableUnpinnedSpace(
        views::SizeBound(available_unpinned_width));
    unpinned_width = std::min(unpinned_width, available_unpinned_width);
  }
  gfx::Rect unpinned_bounds(x, 0, unpinned_width, container_height);
  layouts.child_layouts.emplace_back(unpinned_tabs_scroll_view,
                                     unpinned_tabs_scroll_view->GetVisible(),
                                     unpinned_bounds);
  x += unpinned_width;

  if (scroll_button_container) {
    if (show_scroll_buttons) {
      gfx::Rect scroll_container_bounds(
          x, 0, scroll_button_container_preferred_width, container_height);
      layouts.child_layouts.emplace_back(scroll_button_container, true,
                                         scroll_container_bounds);
      x += scroll_button_container_preferred_width;
    } else {
      layouts.child_layouts.emplace_back(scroll_button_container, false,
                                         gfx::Rect());
    }
  }

  int total_host_width =
      has_unpinned ? std::max(pinned_width, x) : pinned_width;

  layouts.host_size = gfx::Size(total_host_width, container_height);
  return layouts;
}

views::ProposedLayout TabStripViewLayout::CalculateVerticalLayout(
    const TabStripView* tab_strip_view,
    const views::SizeBounds& size_bounds) const {
  views::ProposedLayout layouts;
  views::ScrollView* pinned_tabs_scroll_view =
      tab_strip_view->pinned_tabs_scroll_view();
  views::ScrollView* unpinned_tabs_scroll_view =
      tab_strip_view->unpinned_tabs_scroll_view();
  views::Separator* tabs_separator = tab_strip_view->GetTabsSeparator();

  if (!size_bounds.width().is_bounded()) {
    return layouts;
  }
  const int width = size_bounds.width().value();

  // Avora: every section of the side nav -- Favorites, Live Folders, the
  // pinned and imported sections, the Space label, the new-tab button and the
  // daily tab list -- now lives inside the unpinned ScrollView's contents
  // (AvoraSideNavContentsLayout), so this layout has a single job: give that
  // scroll view the whole strip.  The contents are free to be taller than the
  // viewport, and that overflow is what scrolls, as one surface.
  //
  // Upstream stacked the sections here, above the scroll view, and handed the
  // scroll view only what was left.  That is what made the nav scroll in two
  // pieces, and what let the viewport be sized past the bottom of the window.
  //
  // Chromium's pinned scroll view and its separator are unused in Avora --
  // standalone pinned tabs are drawn by AvoraPinnedSectionView -- so they are
  // laid out hidden and empty rather than given any space.
  layouts.child_layouts.emplace_back(pinned_tabs_scroll_view, false,
                                     gfx::Rect());
  if (tabs_separator) {
    layouts.child_layouts.emplace_back(tabs_separator, false, gfx::Rect());
  }

  // Bounded is the real case: the region view hands the strip the space it
  // has.  Unbounded only comes up in a preferred-size query, where the
  // natural height of the contents is the honest answer.
  const int height =
      size_bounds.height().is_bounded()
          ? size_bounds.height().value()
          : unpinned_tabs_scroll_view->GetPreferredSize(size_bounds).height();

  layouts.child_layouts.emplace_back(unpinned_tabs_scroll_view,
                                     unpinned_tabs_scroll_view->GetVisible(),
                                     gfx::Rect(0, 0, width, height));
  layouts.host_size = gfx::Size(width, height);
  return layouts;
}
