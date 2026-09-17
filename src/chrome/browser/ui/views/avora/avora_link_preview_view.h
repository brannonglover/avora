// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_VIEW_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"
#include "url/gurl.h"

class BrowserWindowInterface;

namespace content {
class WebContents;
}

namespace views {
class ImageButton;
class WebView;
}  // namespace views

namespace avora {

// Callback invoked when the user clicks "open in new tab".
using LinkPreviewOpenCallback =
    base::RepeatingCallback<void(const GURL&)>;

// Callback invoked when the overlay should be dismissed.
using LinkPreviewDismissCallback = base::RepeatingClosure;

// Full-content overlay that intercepts target="_blank" link navigations.
// Instead of opening a new tab, the destination is rendered inside a centered
// card (≈80 % of window width) overlaid on top of the current page.  The
// upper-right corner has close (×) and "expand" (⛶) buttons stacked vertically.
//
// One view per window, but a preview belongs to a tab: the WebContents being
// previewed is owned by its host tab's LinkPreviewTabState, and this view only
// borrows it while that tab is active.  Switching tabs detaches the preview
// (it keeps living, hidden, like a background tab) and switching back attaches
// it again, so the user returns to exactly the page they left.
class AvoraLinkPreviewView : public views::View,
                             public content::WebContentsDelegate,
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
                       LinkPreviewOpenCallback open_cb,
                       LinkPreviewDismissCallback dismiss_cb);
  AvoraLinkPreviewView(const AvoraLinkPreviewView&) = delete;
  AvoraLinkPreviewView& operator=(const AvoraLinkPreviewView&) = delete;
  ~AvoraLinkPreviewView() override;

  // Shows |preview| (owned by |host_tab|'s LinkPreviewTabState) in the card.
  // Does not navigate: the caller starts the load after the view is sized, so
  // the renderer never sees a zero-sized viewport.
  void AttachPreview(content::WebContents* host_tab,
                     content::WebContents* preview,
                     const GURL& url);

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

  // content::WebContentsDelegate:
  content::WebContents* AddNewContents(
      content::WebContents* source,
      std::unique_ptr<content::WebContents> new_contents,
      const GURL& target_url,
      WindowOpenDisposition disposition,
      const blink::mojom::WindowFeatures& window_features,
      bool user_gesture,
      bool* was_blocked) override;

  // content::WebContentsObserver:
  void WebContentsDestroyed() override;

 private:
  void BuildUI();
  void LayoutCard();

  void OnCloseClicked();
  void OnOpenInNewTabClicked();

  raw_ptr<BrowserWindowInterface> browser_;
  LinkPreviewOpenCallback open_cb_;
  LinkPreviewDismissCallback dismiss_cb_;

  raw_ptr<views::View> backdrop_ = nullptr;
  raw_ptr<views::View> card_ = nullptr;
  raw_ptr<views::WebView> web_view_ = nullptr;
  raw_ptr<views::ImageButton> close_button_ = nullptr;
  raw_ptr<views::ImageButton> open_tab_button_ = nullptr;

  GURL previewed_url_;

  // Borrowed from the host tab for as long as that tab stays active; the tab
  // owns it.  Observed so a preview torn down underneath us (tab closed while
  // active) cannot leave a dangling pointer behind.
  raw_ptr<content::WebContents> attached_preview_ = nullptr;
  base::WeakPtr<content::WebContents> host_tab_;

  base::WeakPtrFactory<AvoraLinkPreviewView> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_VIEW_H_
