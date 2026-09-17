// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_TAB_STATE_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_TAB_STATE_H_

#include <memory>

#include "base/callback_list.h"
#include "base/functional/callback_forward.h"
#include "base/supports_user_data.h"
#include "url/gurl.h"

class BrowserWindowInterface;

namespace content {
class WebContents;
}

namespace avora {

// The link preview overlay belongs to the tab the link was clicked in, not to
// the window: switching tabs parks the preview instead of dismissing it, and
// coming back re-attaches the very same WebContents, still where the user left
// it.  The live preview therefore hangs off its host tab's WebContents, which
// also means closing the tab disposes of the preview with it.
//
// Only one preview per tab.  Opening a second target="_blank" link while a
// preview is already parked on the tab falls through to normal new-tab
// behaviour (see BrowserWebContentsDelegate::AddNewContents).
class LinkPreviewTabState : public base::SupportsUserData::Data {
 public:
  LinkPreviewTabState(std::unique_ptr<content::WebContents> preview,
                      const GURL& url);
  LinkPreviewTabState(const LinkPreviewTabState&) = delete;
  LinkPreviewTabState& operator=(const LinkPreviewTabState&) = delete;
  ~LinkPreviewTabState() override;

  // The state parked on |tab|, or null when the tab has no preview open.
  static LinkPreviewTabState* Get(content::WebContents* tab);

  // Parks |preview| on |tab|, replacing (and destroying) any preview the tab
  // already carried.
  static void Set(content::WebContents* tab,
                  std::unique_ptr<content::WebContents> preview,
                  const GURL& url);

  // Dismisses the preview parked on |tab|, destroying its WebContents.
  static void Clear(content::WebContents* tab);

  content::WebContents* preview_contents() const { return preview_.get(); }
  const GURL& previewed_url() const { return url_; }

 private:
  std::unique_ptr<content::WebContents> preview_;
  GURL url_;
};

// Convenience accessors for the common "does this tab carry a preview" checks.
// All tolerate a null |tab|.
bool TabHasLinkPreview(content::WebContents* tab);
content::WebContents* GetLinkPreviewContents(content::WebContents* tab);
GURL GetLinkPreviewUrl(content::WebContents* tab);

// Creates an unnavigated WebContents suitable for previewing |url| in
// |browser|.  Previews render in the Space's own partition, so an isolated
// Space previews links as the identity that Space belongs to.
std::unique_ptr<content::WebContents> CreateLinkPreviewContents(
    BrowserWindowInterface* browser,
    const GURL& url);

// Fires whenever a tab gains or loses its preview, with the host tab as the
// argument.  Tab views subscribe so they can show or hide the marker telling
// the user a preview is parked behind that tab.
base::CallbackListSubscription AddLinkPreviewStateChangedCallback(
    base::RepeatingCallback<void(content::WebContents* tab)> callback);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_TAB_STATE_H_
