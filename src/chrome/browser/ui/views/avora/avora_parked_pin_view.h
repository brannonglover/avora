// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PARKED_PIN_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PARKED_PIN_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/tabs/tab_change_type.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class TabStripModel;

namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

namespace views {
class Label;
}  // namespace views

namespace avora {

// "Closing" a pinned tab that cannot survive being closed.
//
// A pinned row's minus normally closes the live tab and leaves the persisted
// pinned item to bring it back.  A natively pinned tab with no pinned item
// behind it -- in practice a page that cannot be persisted as one, such as
// the New Tab page or a chrome:// page -- has nothing to bring it back, so
// closing it would quietly unpin it.  Instead it is *parked*: the tab stays
// open and pinned, and the content area shows a quote over it until the user
// asks for the page again by clicking the pin, or navigates the tab.
//
// Parked state lives on the WebContents, so it follows the tab and needs no
// bookkeeping when the tab closes.  Each call notifies |tab_strip| with
// TabChangeType::kAll, which is how AvoraParkedPinView finds out.
void ParkPinnedTab(TabStripModel* tab_strip, content::WebContents* contents);
void UnparkPinnedTab(TabStripModel* tab_strip, content::WebContents* contents);
bool IsPinnedTabParked(content::WebContents* contents);

// The quote shown over the content area while the active tab is parked.  One
// per window, laid out over the content area by BrowserViewLayout; it shows
// and hides itself by following the window's active tab.
class AvoraParkedPinView : public views::View,
                           public TabStripModelObserver,
                           public content::WebContentsObserver {
  METADATA_HEADER(AvoraParkedPinView, views::View)

 public:
  explicit AvoraParkedPinView(TabStripModel* tab_strip);
  AvoraParkedPinView(const AvoraParkedPinView&) = delete;
  AvoraParkedPinView& operator=(const AvoraParkedPinView&) = delete;
  ~AvoraParkedPinView() override;

  // views::View:
  void OnPaintBackground(gfx::Canvas* canvas) override;
  void Layout(PassKey) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;

  // content::WebContentsObserver:
  void DidStartNavigation(
      content::NavigationHandle* navigation_handle) override;

 private:
  // Shows the quote for the active tab when it is parked, hides it otherwise.
  void Refresh();

  raw_ptr<TabStripModel> tab_strip_;
  raw_ptr<views::View> text_column_ = nullptr;
  raw_ptr<views::Label> quote_label_ = nullptr;
  raw_ptr<views::Label> attribution_label_ = nullptr;
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PARKED_PIN_VIEW_H_
