// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/layout/browser_view_popup_layout_impl.h"

#include "chrome/browser/ui/views/avora/avora_mini_window.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/infobars/infobar_container_view.h"
#include "ui/views/controls/separator.h"

BrowserViewPopupLayoutImpl::BrowserViewPopupLayoutImpl(
    std::unique_ptr<BrowserViewLayoutDelegate> delegate,
    BrowserViewLayoutViews views)
    : BrowserViewLayoutImpl(std::move(delegate), std::move(views)) {
  // Some elements may be visible when they should not be. Remove them.
  if (this->views().horizontal_tab_strip_region_view) {
    this->views().horizontal_tab_strip_region_view->SetVisible(false);
  }
}

BrowserViewPopupLayoutImpl::~BrowserViewPopupLayoutImpl() = default;

gfx::Size BrowserViewPopupLayoutImpl::GetMinimumSize(
    const views::View* host) const {
  const auto params =
      delegate().GetBrowserLayoutParams(/*use_browser_bounds=*/false);
  const auto leading = params.leading_exclusion.ContentWithPadding();
  const auto trailing = params.trailing_exclusion.ContentWithPadding();
  const gfx::Size caption_size(
      base::ClampCeil(leading.width() + trailing.width()),
      base::ClampCeil(std::max(leading.height(), trailing.height())));

  const gfx::Size toolbar_size =
      views().toolbar && views().toolbar->GetVisible()
          ? views().toolbar->GetMinimumSize()
          : gfx::Size();

  const int separator_height =
      views().top_container_separator &&
              views().top_container_separator->GetVisible()
          ? views::Separator::kThickness
          : 0;

  constexpr gfx::Size kMinContentsSize(1, 1);

  // Avora: the mini window is a window for reading a page in, so it has a
  // floor of its own rather than the one-pixel contents a popup would settle
  // for.  Taken as a whole-window minimum, which is what the window manager
  // enforces when the user drags an edge.
  if (views().avora_mini_toolbar) {
    return gfx::Size(
        std::max({avora::kMiniWindowMinWidth, caption_size.width(),
                  toolbar_size.width()}),
        std::max(avora::kMiniWindowMinHeight,
                 kMinContentsSize.height() + caption_size.height() +
                     toolbar_size.height() + separator_height));
  }

  return gfx::Size(std::max({kMinContentsSize.width(), caption_size.width(),
                             toolbar_size.width()}),
                   kMinContentsSize.height() + caption_size.height() +
                       toolbar_size.height() + separator_height);
}

BrowserViewPopupLayoutImpl::ProposedLayout
BrowserViewPopupLayoutImpl::CalculateProposedLayout(
    const BrowserLayoutParams& browser_params) const {
  ProposedLayout layout;

  // The window scrim covers the entire browser view.
  if (views().window_scrim) {
    layout.AddChild(views().window_scrim, browser_params.visual_client_area);
  }

  BrowserLayoutParams params = browser_params;

  // Lay out the top container if it's in the browser (even if it's empty).
  // This is required for certain things like hit-testing.
  if (IsParentedTo(views().top_container, views().browser_view)) {
    auto& top_container_layout =
        layout.AddChild(views().top_container, gfx::Rect());
    const gfx::Rect top_container_local_bounds = CalculateTopContainerLayout(
        top_container_layout,
        params.InLocalCoordinates(params.visual_client_area),
        /*needs_exclusion=*/false);
    top_container_layout.bounds =
        GetTopContainerBoundsInParent(top_container_local_bounds, params);
    // In cases where the top container is nonzero size, need to move everything
    // else down.
    params.SetTop(std::max(params.visual_client_area.y(),
                           top_container_layout.bounds.bottom()));
  }

  // Lay out infobar container if present.
  if (IsParentedTo(views().infobar_container, views().browser_view)) {
    gfx::Rect infobar_bounds;
    const bool infobar_visible = delegate().IsInfobarVisible();
    if (infobar_visible) {
      infobar_bounds = gfx::Rect(
          params.visual_client_area.x(),
          // Infobar needs to get down out of the way of immersive mode elements
          // in some cases.
          params.visual_client_area.y() + delegate().GetExtraInfobarOffset(),
          params.visual_client_area.width(),
          // This returns zero for empty infobar.
          views().infobar_container->GetPreferredSize().height());
      params.SetTop(infobar_bounds.bottom());
    }
    layout.AddChild(views().infobar_container, infobar_bounds, infobar_visible);
  }

  // Lay out contents container.
  CHECK(IsParentedToAndVisible(views().multi_contents_view,
                               views().browser_view));
  gfx::Rect contents_bounds = params.visual_client_area;
  contents_bounds.set_height(std::max(contents_bounds.height(), 1));
  layout.AddChild(views().multi_contents_view, contents_bounds);

  // Avora: the confirmation toast floats over the upper-right of the page,
  // the same corner it occupies in the main window, so Cmd-Shift-C reads
  // identically in both.
  if (views().avora_toast &&
      IsParentedTo(views().avora_toast, views().browser_view)) {
    constexpr int kToastMargin = 2;
    const gfx::Size toast_size = views().avora_toast->GetPreferredSize();
    gfx::Rect toast_bounds(
        contents_bounds.right() - kToastMargin - toast_size.width(),
        contents_bounds.y() + kToastMargin, toast_size.width(),
        toast_size.height());
    layout.AddChild(views().avora_toast, toast_bounds,
                    views().avora_toast->GetVisible());
  }

  return layout;
}

gfx::Rect BrowserViewPopupLayoutImpl::CalculateTopContainerLayout(
    ProposedLayout& layout,
    BrowserLayoutParams params,
    bool needs_exclusion) const {
  const int original_top = params.visual_client_area.y();

  // Layout starts beneath the caption buttons because title is laid out by the
  // OS in popup mode.
  params.SetTop(base::ClampCeil(
      std::max(params.leading_exclusion.ContentWithPadding().height(),
               params.trailing_exclusion.ContentWithPadding().height())));

  // Avora: the mini window's own chrome sits where the location bar would,
  // and the standard toolbar stays hidden behind it.  IsToolbarVisible()
  // reports on the toolbar's existence rather than its visibility (extension
  // popups and other callers rely on that), so the mini window is recognised
  // here by the chrome it actually carries.
  const bool show_mini_toolbar =
      views().avora_mini_toolbar &&
      IsParentedTo(views().avora_mini_toolbar, views().top_container);

  // Lay out the standard toolbar if present. This is used in tab fullscreen
  // when custom tabs are present.
  const bool show_toolbar = delegate().IsToolbarVisible() && !show_mini_toolbar;

  // Whichever chrome occupies the top of the window gets the same rect.  The
  // hidden standard toolbar is given the mini window's rect rather than a zero
  // height one on purpose: web-modal dialogs hang from the toolbar's bottom
  // edge (BrowserViewLayoutImpl::GetDialogTop), so at zero height the external
  // protocol prompt and its kind would hang above the top of the window
  // instead of just under the chrome the user can see.
  gfx::Rect chrome_bounds(params.visual_client_area.origin(), gfx::Size());
  if (show_toolbar) {
    chrome_bounds.set_width(params.visual_client_area.width());
    chrome_bounds.set_height(views().toolbar->GetPreferredSize().height());
  } else if (show_mini_toolbar) {
    chrome_bounds.set_width(params.visual_client_area.width());
    chrome_bounds.set_height(
        views().avora_mini_toolbar->GetPreferredSize().height());
  }

  if (IsParentedTo(views().toolbar, views().top_container)) {
    layout.AddChild(views().toolbar, chrome_bounds, show_toolbar);
    params.SetTop(chrome_bounds.bottom());
  }

  if (show_mini_toolbar) {
    layout.AddChild(views().avora_mini_toolbar, chrome_bounds);
    params.SetTop(chrome_bounds.bottom());
  }

  // Add the top container separator. This is always present in popups but not
  // in picture-in-picture.
  if (IsParentedTo(views().top_container_separator, views().top_container)) {
    // If there's no toolbar, even if a separator could be shown, it will not
    // be.
    const bool show_separator = delegate().IsContentsSeparatorEnabled() &&
                                (show_toolbar || show_mini_toolbar);
    gfx::Rect separator_bounds;
    if (show_separator) {
      separator_bounds = gfx::Rect(
          params.visual_client_area.x(), params.visual_client_area.y(),
          params.visual_client_area.width(), views::Separator::kThickness);
      params.SetTop(separator_bounds.bottom());
    }
    layout.AddChild(views().top_container_separator, separator_bounds,
                    show_separator);
  }

  return gfx::Rect(params.visual_client_area.x(), original_top,
                   params.visual_client_area.width(),
                   params.visual_client_area.y() - original_top);
}
