// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_IMPORT_OFFER_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_IMPORT_OFFER_VIEW_H_

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
class AvoraImportOfferView : public views::DialogDelegateView {
  METADATA_HEADER(AvoraImportOfferView, views::DialogDelegateView)

 public:
  // Shows the offer dialog if appropriate (first run, browsers detected).
  // Returns true if the dialog was shown, false if suppressed.
  static bool MaybeShow(BrowserWindowInterface* browser,
                        WindowSpaceState* window_space_state);

  AvoraImportOfferView(BrowserWindowInterface* browser,
                       WindowSpaceState* window_space_state,
                       std::vector<DetectedBrowser> browsers);
  AvoraImportOfferView(const AvoraImportOfferView&) = delete;
  AvoraImportOfferView& operator=(const AvoraImportOfferView&) = delete;
  ~AvoraImportOfferView() override;

  // views::DialogDelegateView:
  bool Accept() override;

 private:
  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<WindowSpaceState> window_space_state_;
  std::vector<DetectedBrowser> browsers_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_IMPORT_OFFER_VIEW_H_
