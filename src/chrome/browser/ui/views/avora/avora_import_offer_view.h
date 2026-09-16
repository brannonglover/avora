// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_IMPORT_OFFER_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_IMPORT_OFFER_VIEW_H_

#include <memory>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/avora/import/bookmark_import_types.h"
#include "ui/views/view.h"
#include "ui/views/window/dialog_delegate.h"

class BrowserWindowInterface;

namespace avora {

class WindowSpaceState;

// A lightweight first-run dialog that detects installed browsers and
// offers to import bookmarks.  Shown once per profile; the pref
// avora.import_offered is set after display (accepted or dismissed).
//
// If the user clicks "Import bookmarks", the standard import dialog
// is opened.  If "Not now", the dialog closes and the sidebar
// "Import Bookmarks…" link remains available for later use.
//
// This view is the contents view of a plain views::DialogDelegate rather
// than a views::DialogDelegateView subclass; see the comment on
// `delegate_` for the ownership contract.
class AvoraImportOfferView : public views::View {
  METADATA_HEADER(AvoraImportOfferView, views::View)

 public:
  // Shows the offer dialog if appropriate (first run, browsers detected).
  // Returns true if the dialog was shown, false if suppressed.
  static bool MaybeShow(BrowserWindowInterface* browser,
                        WindowSpaceState* window_space_state);

  AvoraImportOfferView(std::unique_ptr<views::DialogDelegate> delegate,
                       BrowserWindowInterface* browser,
                       WindowSpaceState* window_space_state,
                       std::vector<DetectedBrowser> browsers);
  AvoraImportOfferView(const AvoraImportOfferView&) = delete;
  AvoraImportOfferView& operator=(const AvoraImportOfferView&) = delete;
  ~AvoraImportOfferView() override;

 private:
  // Runs when the user presses "Import bookmarks".  The dialog closes
  // once this returns.
  void OnAccept();

  // The DialogDelegate hosting this view.  Widget only holds a WeakPtr to
  // its delegate and never deletes it, and neither DeleteDelegate() nor the
  // SetOwnedByWidget()/RegisterDeleteDelegateCallback() hooks are available
  // to code outside //ui/views' friend lists.  Anchoring the delegate here
  // works because Widget always calls WidgetDelegate::DeleteDelegate()
  // before DestroyRootView(), so the delegate is still alive while the
  // Widget needs it and is torn down with the view hierarchy afterwards,
  // satisfying the "a WidgetDelegate must outlive its Widget" CHECK.
  std::unique_ptr<views::DialogDelegate> delegate_;

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<WindowSpaceState> window_space_state_;
  std::vector<DetectedBrowser> browsers_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_IMPORT_OFFER_VIEW_H_
