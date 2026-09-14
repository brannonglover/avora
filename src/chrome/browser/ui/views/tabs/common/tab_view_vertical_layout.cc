// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/common/tab_view_vertical_layout.h"

#include "chrome/browser/glic/browser_ui/tab_underline_view.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/shared/tab_strip_types.h"
#include "chrome/browser/ui/views/tabs/tab/alert_indicator_button.h"
#include "chrome/browser/ui/views/tabs/tab/tab_close_button.h"
#include "chrome/browser/ui/views/tabs/tab/tab_icon.h"
#include "chrome/browser/ui/views/tabs/tab/tab_title.h"
#include "chrome/browser/ui/views/tabs/tab_style_views.h"

namespace {
constexpr int kIconDesignWidth = 16;
constexpr int kTitleMinWidth = 10;
// Avora: align favicon with the "Default Space" label (10px left) and
// give breathing room between icon and title text.
constexpr int kHorizontalInset = 8;
constexpr int kDefaultPadding = 6;
}  // namespace

TabViewVerticalLayout::TabViewVerticalLayout() = default;
TabViewVerticalLayout::~TabViewVerticalLayout() = default;

void TabViewVerticalLayout::OnInstalled(views::View* host) {
  TabView::LayoutManager::OnInstalled(host);
  tab_children_configs_ = {
      TabChildConfig(TabView().close_button_, kIconDesignWidth, kDefaultPadding,
                     /*align_leading=*/false,
                     /*expand=*/false),
      TabChildConfig(TabView().alert_indicator_, kIconDesignWidth,
                     kDefaultPadding,
                     /*align_leading=*/false,
                     /*expand=*/false,
                     /*decorate_on_collapse=*/true),
      TabChildConfig(TabView().icon_, kIconDesignWidth, kHorizontalInset,
                     /*align_leading=*/true,
                     /*expand=*/false),
      TabChildConfig(TabView().title_, kTitleMinWidth, kDefaultPadding,
                     /*align_leading=*/true,
                     /*expand=*/true)};
}

views::ProposedLayout TabViewVerticalLayout::CalculateProposedLayout(
    const views::SizeBounds& size_bounds) const {
  const int width = size_bounds.width().value_or(
      VerticalTabStripRegionView::kUncollapsedMaxWidth);
  // Avora: pinned tabs use the same height as unpinned tabs.
  const int height = GetLayoutConstant(LayoutConstant::kVerticalTabHeight);
  views::ProposedLayout layouts;
  layouts.host_size = gfx::Size(width, height);

  gfx::Rect bounds_remaining = gfx::Rect(layouts.host_size);
  bounds_remaining.Inset(TabView().tab_styling()->GetContentsInsets());

  // If the tab is collapsed but animating with a wider width then we shouldn't
  // center the contents.
  // Avora: pinned tabs use the same full layout as unpinned tabs (show title,
  // close button, etc.) so they are not centered.
  const bool is_centered = TabView().collapsed_ &&
                           !TabView().IsInExpandOnHover(width);

  int placed_children = 0;
  for (const auto& child : tab_children_configs_) {
    const bool can_render_child =
        is_centered
            ? (placed_children == 0)
            : (child.min_width + child.padding < bounds_remaining.width() ||
               placed_children < 2);
    const bool is_child_visible = IsChildVisible(child.view, width);
    if (is_child_visible && can_render_child) {
      layouts.child_layouts.emplace_back(
          child.view.get(), is_child_visible,
          GetChildBounds(bounds_remaining, child, is_centered));

      if (!is_centered) {
        bounds_remaining.Inset(
            child.align_leading
                ? gfx::Insets().set_left(child.padding + child.min_width)
                : gfx::Insets().set_right(child.padding + child.min_width));
      }

      placed_children += 1;
    } else if (child.decorate_on_collapse) {
      layouts.child_layouts.emplace_back(
          child.view.get(), is_child_visible,
          gfx::Rect(width / 2, height / 2, 0, 0));
    } else {
      layouts.child_layouts.emplace_back(
          child.view.get(), is_child_visible,
          gfx::Rect(bounds_remaining.x(), bounds_remaining.y(), 0, 0));
    }
  }

  if (TabView().glic_tab_underline_view_) {
    const gfx::Rect glic_bounds =
        gfx::Rect(0, 0, 2 * glic::TabUnderlineView::kEffectThickness, height);
    layouts.child_layouts.emplace_back(
        TabView().glic_tab_underline_view_.get(),
        TabView().glic_tab_underline_view_->GetVisible(), glic_bounds);
  }

  return layouts;
}

gfx::Rect TabViewVerticalLayout::GetChildBounds(const gfx::Rect& container,
                                                const TabChildConfig& config,
                                                const bool center) const {
  int preferred_width;
  int preferred_height;
  if (config.expand) {
    preferred_width = container.width() - config.padding;
    // The only expandable view is the views::Label. Just get the line height to
    // make calculating bounds cheaper.
    views::Label* label = views::AsViewClass<views::Label>(config.view);
    CHECK(label);
    preferred_height = label->GetLineHeight();
  } else {
    const gfx::Size preferred_size = config.view->GetPreferredSize();
    preferred_width = preferred_size.width();
    preferred_height = preferred_size.height();
  }

  // Some icons have larger sizes to account for decoration. Make a distinction
  // between the design width and the actual width.
  const int design_width =
      config.expand ? container.width() - config.padding : config.min_width;

  int x = container.x();
  if (center) {
    x += 0.5 * (container.width() - preferred_width);
  } else if (config.align_leading) {
    x += 0.5 * (design_width - preferred_width);
  } else {
    x += container.width() - 0.5 * (design_width + preferred_width);
  }
  const int y = container.y() + 0.5 * (container.height() - preferred_height);

  return gfx::Rect(x, y, preferred_width, preferred_height);
}

bool TabViewVerticalLayout::IsChildVisible(const views::View* child_view,
                                           const int width) const {
  if (child_view == TabView().title_) {
    // Avora: pinned tabs show the title, same as unpinned tabs.
    return true;
  }

  if (child_view == TabView().alert_indicator_) {
    if (TabView().glic_tab_underline_view_ &&
        (TabView().alert_indicator_->showing_alert_state() ==
             tabs::TabAlert::kGlicAccessing ||
         TabView().alert_indicator_->showing_alert_state() ==
             tabs::TabAlert::kGlicSharing)) {
      return false;
    }
    return TabView().alert_indicator_->showing_alert_state().has_value();
  }

  if (child_view == TabView().icon_) {
    // Avora: always show the icon for pinned tabs (same as unpinned).
    return true;
  }

  if (child_view == TabView().close_button_) {
    // Avora: pinned tabs show the close button on hover, same as unpinned.
    const bool hovered_or_focused =
        TabView().hovered_ || TabView().HasFocus() ||
        (TabView().close_button_ && TabView().close_button_->HasFocus());
    return hovered_or_focused;
  }

  NOTREACHED() << "Unknown tab child view";
}
