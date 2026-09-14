// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_VIEW_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_delegate.h"
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
class AvoraLinkPreviewView : public views::View,
                             public content::WebContentsDelegate {
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

  // Show the overlay, loading the given URL in the embedded web view.
  void Show(const GURL& url);

  // Show the overlay with already-created WebContents (from AddNewContents).
  void ShowWithContents(std::unique_ptr<content::WebContents> contents,
                        const GURL& url);

  // Hide the overlay and destroy the web contents.
  void Hide();

  bool IsShowing() const;

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
  std::unique_ptr<content::WebContents> owned_contents_;

  base::WeakPtrFactory<AvoraLinkPreviewView> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_VIEW_H_
