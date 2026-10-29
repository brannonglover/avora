// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_TAB_STATE_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_TAB_STATE_H_

#include <memory>

#include "base/callback_list.h"
#include "base/functional/callback_forward.h"
#include "base/supports_user_data.h"
#include "components/tabs/public/tab_interface.h"
#include "url/gurl.h"

class BrowserWindowInterface;

namespace content {
class WebContents;
}

namespace avora {

// The link preview overlay belongs to the tab the link was clicked in, not to
// the window: switching tabs parks the preview instead of dismissing it, and
// coming back re-attaches the very same page, still where the user left it.
//
// The previewed page is a real tab in the host tab's TabStripModel, marked
// with MarkLinkPreviewTab() and filtered out of the daily list by
// AvoraSpaceTabFilter.  It used to be a free-standing WebContents that no tab
// strip owned, which cost it everything Chromium keys off tab-strip
// membership: ExtensionTabUtil::GetTabById walks the strips of live windows,
// so chrome.tabs could not resolve the preview at all and every extension
// that round-trips through that API -- password managers above all -- failed
// inside the overlay even though its content scripts had been injected.
// Being a genuine tab also hands the preview the browser as its
// WebContentsDelegate, so dialogs, permission prompts and downloads behave
// there exactly as they do anywhere else.
//
// This class is the host tab -> preview tab association, held as a durable
// tabs::TabHandle rather than a WebContents pointer so it survives the
// contents swap a discarded tab goes through.  Only one preview per tab.
class LinkPreviewTabState : public base::SupportsUserData::Data {
 public:
  LinkPreviewTabState(tabs::TabHandle preview, const GURL& url);
  LinkPreviewTabState(const LinkPreviewTabState&) = delete;
  LinkPreviewTabState& operator=(const LinkPreviewTabState&) = delete;

  // Closes the preview tab, so closing a host tab takes its preview with it.
  ~LinkPreviewTabState() override;

  // The state parked on |tab|, or null when the tab has no preview open.
  static LinkPreviewTabState* Get(content::WebContents* tab);

  // Adopts |preview| into |tab|'s tab strip as a hidden background tab and
  // parks it on |tab|, replacing (and closing) any preview already there.
  //
  // Returns null once the preview has been adopted.  When |tab| is not in a
  // tab strip there is nowhere to adopt it into, and |preview| comes back to
  // the caller rather than being dropped: a page the renderer already built
  // has to end up somewhere, so the caller opens it as an ordinary tab.
  [[nodiscard]] static std::unique_ptr<content::WebContents> Set(
      content::WebContents* tab,
      std::unique_ptr<content::WebContents> preview,
      const GURL& url);

  // Dismisses the preview parked on |tab|, closing its tab.
  static void Clear(content::WebContents* tab);

  // Releases the preview without closing it, leaving an ordinary tab behind.
  // Returns the promoted contents, or null when |tab| had no preview.
  static content::WebContents* Promote(content::WebContents* tab);

  content::WebContents* preview_contents() const;
  tabs::TabHandle preview_handle() const { return preview_; }
  const GURL& previewed_url() const { return url_; }

 private:
  // Hands the preview tab back to the caller, so this object's destructor
  // stops treating it as something to close.
  tabs::TabHandle TakePreview();

  tabs::TabHandle preview_;
  GURL url_;
};

// Convenience accessors for the common "does this tab carry a preview" checks.
// All tolerate a null |tab|.
bool TabHasLinkPreview(content::WebContents* tab);
content::WebContents* GetLinkPreviewContents(content::WebContents* tab);
GURL GetLinkPreviewUrl(content::WebContents* tab);

// True when |preview|, the preview parked on |host|, was opened for a link
// to another origin and is now showing a page on the site |host| belongs to
// -- the shape of a sign-in that started in the card and finished there.  The
// session that sign-in produced belongs to the host tab, which is still
// showing the page from before the user logged in.
//
// "The site |host| belongs to" is any of: the origin it is showing, the
// origin it originally asked for before redirects bounced it elsewhere (a
// logged-out Gmail redirects to a marketing page), and the saved destination
// of the Favorite or pinned item it backs.
//
// The test is on the link the preview was opened for, not on the first page
// it committed: an identity provider that already has a session answers the
// sign-in link with nothing but redirects, so the very first page the
// preview commits is the host's own.  A link to the host's own site is just
// a link, however far the user browses inside the card afterwards.
bool IsLinkPreviewReturnToHost(content::WebContents* host,
                               content::WebContents* preview);

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

// Announces a preview change that did not go through Set(), Clear() or
// Promote().  The one caller is the promotion path's fallback: when the host
// tab is already gone there is no state object left to promote, so the mark
// is dropped directly and this is what tells the tab strip about it.  A mark
// dropped in silence leaves the tab hidden and untagged -- open and active,
// with no row anywhere.  |tab| only has to be some tab in the window whose
// strip should be re-examined; the promoted tab itself is the natural one.
void NotifyLinkPreviewStateChanged(content::WebContents* tab);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_TAB_STATE_H_
