// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_VIEW_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/view.h"
#include "ui/views/view_targeter_delegate.h"
#include "url/gurl.h"

class BrowserWindowInterface;

namespace content {
class NavigationHandle;
class WebContents;
}

namespace views {
class ImageButton;
class WebView;
}  // namespace views

namespace avora {

// Callback invoked when the user clicks "expand into a tab".  It carries no
// URL on purpose: expanding promotes the live preview tab itself, so the page
// the user has been working in keeps its session, scroll position, form state
// and open connections instead of being re-fetched from its address.
using LinkPreviewPromoteCallback = base::RepeatingClosure;

// Callback invoked when the overlay should be dismissed.
using LinkPreviewDismissCallback = base::RepeatingClosure;

// Callback invoked when the attached preview has landed back on its host
// tab's site -- a sign-in finishing in the card; see
// IsLinkPreviewReturnToHost().  Carries the page it landed on, which the
// host should load in its place.
using LinkPreviewReturnCallback = base::RepeatingCallback<void(const GURL&)>;

// Full-content overlay that intercepts target="_blank" link navigations.
// Instead of opening a new tab, the destination is rendered inside a centered
// card (≈80 % of window width) overlaid on top of the current page.  The
// upper-right corner has close (×) and "expand" (⛶) buttons stacked vertically.
//
// One view per window, but a preview belongs to a tab: the page being
// previewed is a hidden background tab parked on its host tab by
// LinkPreviewTabState, and this view only borrows it while that tab is
// active.  Switching tabs detaches the preview (it keeps living as the
// background tab it already is) and switching back attaches it again, so the
// user returns to exactly the page they left.
//
// This view is deliberately NOT the preview's WebContentsDelegate.  The
// preview is a real tab, so the browser is its delegate like any other tab's,
// which is what keeps dialogs, permission prompts, downloads and extension
// UI working inside the card.
class AvoraLinkPreviewView : public views::View,
                             public views::ViewTargeterDelegate,
                             public content::WebContentsObserver {
  METADATA_HEADER(AvoraLinkPreviewView, views::View)

 public:
  static constexpr int kCardWidth = 1200;
  static constexpr int kCardCornerRadius = 16;
  static constexpr int kCardVerticalMargin = -5;
  static constexpr int kButtonSize = 44;
  static constexpr int kButtonMargin = 12;
  static constexpr int kButtonSpacing = 8;
  static constexpr int kButtonIconSize = 28;

  AvoraLinkPreviewView(BrowserWindowInterface* browser,
                       LinkPreviewPromoteCallback promote_cb,
                       LinkPreviewDismissCallback dismiss_cb,
                       LinkPreviewReturnCallback return_cb);
  AvoraLinkPreviewView(const AvoraLinkPreviewView&) = delete;
  AvoraLinkPreviewView& operator=(const AvoraLinkPreviewView&) = delete;
  ~AvoraLinkPreviewView() override;

  // Shows the card chrome for |preview| (the tab parked on |host_tab|).
  //
  // This view does NOT host the page.  MultiContentsView's secondary contents
  // container does, positioned at card_bounds(); that is what makes the
  // preview a real active tab and lets Chromium's per-tab UI -- modal
  // dialogs, DevTools, Sad Tab -- resolve to the card on their own.  This
  // view owns everything around the page: the backdrop, the card's geometry,
  // and the buttons.
  void AttachPreview(content::WebContents* host_tab,
                     content::WebContents* preview,
                     const GURL& url);

  // Where the page should be mounted, in this view's coordinates.  Empty
  // until the view has been laid out.
  gfx::Rect card_bounds() const;

  // Corner radii the page should be drawn with, to match the card chrome.
  static gfx::RoundedCornersF card_radii();

  // Runs whenever card_bounds() changes so the embedder can keep the contents
  // container in step.
  using CardBoundsChangedCallback = base::RepeatingClosure;
  void set_card_bounds_changed_callback(CardBoundsChangedCallback callback) {
    card_bounds_changed_ = std::move(callback);
  }

  // Hides the overlay while leaving the preview alive on its host tab.
  void DetachPreview();

  bool IsShowing() const;

  // The preview currently on screen, or null when nothing is attached.
  content::WebContents* attached_preview() const { return attached_preview_; }

  // The tab the attached preview belongs to, or null when nothing is attached.
  content::WebContents* host_tab() const { return host_tab_.get(); }

  // The URL currently being previewed.
  const GURL& previewed_url() const { return previewed_url_; }

  // views::View overrides:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;

  // views::ViewTargeterDelegate:
  // The page lives in a contents container beneath this view, so the card's
  // area must not swallow events on their way to it.
  bool DoesIntersectRect(const views::View* target,
                         const gfx::Rect& rect) const override;

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void WebContentsDestroyed() override;

 private:
  void BuildUI();
  void LayoutCard();

  void OnCloseClicked();
  void OnPromoteToTabClicked();

  // Runs |return_cb_| when the attached preview is sitting on its host's
  // site.  See LinkPreviewReturnCallback.
  void MaybeReturnToHost();

  raw_ptr<BrowserWindowInterface> browser_;
  LinkPreviewPromoteCallback promote_cb_;
  LinkPreviewDismissCallback dismiss_cb_;
  LinkPreviewReturnCallback return_cb_;

  // Dims everything except the card's rect; the page shows through the hole.
  raw_ptr<views::View> backdrop_ = nullptr;
  // Geometry anchor for the page and the buttons.  Paints nothing: the
  // contents container beneath this view draws the page and its corners.
  raw_ptr<views::View> card_ = nullptr;
  raw_ptr<views::ImageButton> close_button_ = nullptr;
  raw_ptr<views::ImageButton> open_tab_button_ = nullptr;

  CardBoundsChangedCallback card_bounds_changed_;

  GURL previewed_url_;

  // Borrowed for as long as the host tab stays active; the tab strip owns
  // it.  Observed so a preview torn down underneath us -- its tab closed, or
  // its contents swapped out by a discard -- cannot leave a dangling pointer
  // behind.
  raw_ptr<content::WebContents> attached_preview_ = nullptr;
  base::WeakPtr<content::WebContents> host_tab_;

  base::WeakPtrFactory<AvoraLinkPreviewView> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_VIEW_H_
