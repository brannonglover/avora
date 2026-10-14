// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_MINI_WINDOW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_MINI_WINDOW_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/avora/avora_space.h"
#include "chrome/browser/avora/avora_window_session_data.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "url/gurl.h"

class BrowserWindowInterface;
class Profile;

namespace content {
class WebContents;
}

namespace avora {

// The mini window's geometry.  Big enough to actually read a page in -- a mini
// window is a page without a tab strip, not a peephole -- and with a floor the
// window manager enforces, so it cannot be dragged down to a sliver where the
// toolbar has nowhere to lay itself out.
inline constexpr int kMiniWindowDefaultWidth = 1400;
inline constexpr int kMiniWindowDefaultHeight = 1080;
inline constexpr int kMiniWindowMinWidth = 480;
inline constexpr int kMiniWindowMinHeight = 400;

// Opens |url| in Avora's mini window, creating the window or reusing the one
// already open.  This is where links arriving from outside Avora land: a link
// clicked in Mail or Slack is a glance, not a commitment, so it gets a window
// of its own instead of a tab in the user's working set.
//
// The mini window deliberately does not become a tabbed window.  It can be the
// only Avora window on screen; a tabbed window is created only if the user
// commits the link to a Space and none exists to receive it.
void ShowMiniWindow(Profile* profile, const GURL& url);

// Whether a mini window is currently open.  False from the moment the window
// is asked to close, so a window still running its unload handlers is never
// offered up for reuse.
bool IsMiniWindowShowing();

// Closes the mini window if one is open.  No-op otherwise.
void CloseMiniWindow();

// The open mini window's browser, or null when none is open (or the open one
// is already on its way out).
BrowserWindowInterface* GetMiniWindowBrowser();

// Whether |browser| is a mini window.  Tolerates null.  Reads the marker the
// mini window publishes on its own window, so it answers for any browser
// window without this component having to be asked about a specific one.
bool IsMiniWindow(const BrowserWindowInterface* browser);

// The Space a mini window opened right now would belong to: the Space of the
// tabbed window the user was last in -- far and away the likeliest destination
// for the link -- else the profile-wide active Space.
std::string PreferredMiniWindowSpaceId(Profile* profile);

// The Spaces the mini window offers, in the order it offers them: |preferred|
// first, then the rest in Space order.  An empty or unknown |preferred| leaves
// the order untouched.
std::vector<Space> OrderMiniWindowSpaces(const std::vector<Space>& spaces,
                                         const std::string& preferred);

// The Spaces the open mini window offers, already ordered.  Empty when no mini
// window is open.
std::vector<Space> MiniWindowOrderedSpaces();

// Commits the open mini window's page to |space_id|, opening it as a new tab
// there and closing the mini window.  Creates a tabbed window only when there
// is none to receive the tab.  No-op when no mini window is open.
void SendMiniWindowToSpace(const std::string& space_id);

// Marks a browser window as Avora's mini window.  Published by
// MiniWindowController on its own window's UnownedUserDataHost, which is what
// lets code holding nothing but a BrowserWindowInterface* recognise one.
class MiniWindowState {
 public:
  DECLARE_USER_DATA(MiniWindowState);

  MiniWindowState();
  ~MiniWindowState();
};

// The mini window: a browser window of its own, holding exactly one tab.
//
// The page used to be a free-standing WebContents this component owned
// outright, with no Browser and no TabStripModel behind it.  That cost the
// mini window everything Chromium keys off window and tab-strip membership.
// ExtensionTabUtil::GetTabById walks the strips of the windows in
// WindowControllerList, so chrome.tabs could not resolve the page at all and
// every extension that round-trips through that API -- password managers above
// all -- failed inside the mini window even though its content scripts had
// been injected.  chrome.action.openPopup() failed for the same reason one
// level up: with no browser window to be the active one, an extension asking
// to show its popup was told there was no window to show it in, which is how a
// click on 1Password's locked padlock came to do nothing at all.  It is the
// same lesson the link preview learned (see LinkPreviewTabState): a page that
// is not a tab in a window is not a page extensions can work with.
//
// Being a real browser window also hands the page a Browser as its
// WebContentsDelegate, so web-modal dialogs, permission prompts and downloads
// behave here exactly as they do anywhere else, with nothing hosted by hand.
//
// The window is a TYPE_POPUP browser, which is what keeps it out of the way of
// the rest of Avora: ProfileBrowserCollection::FindTabbedBrowser skips it, so
// it can never become its own send-to-a-Space target or stand in for the
// window whose Space the selector offers first.  A popup's tab strip also
// refuses new tabs once it holds one (see WindowCanOpenTabs), which is what
// keeps a one-page window one page without any policing here.
//
// At most one mini window exists.  A second external link re-points the open
// one rather than stacking another on screen -- unless the link belongs to a
// different Space than the open window's identity, in which case the window is
// replaced, because a tab pinned to one Space's storage partition cannot host
// another's.
class MiniWindowController {
 public:
  // The open mini window's controller, or null when none is open.
  static MiniWindowController* Get();

  // Creates the window and points it at |url|.  Check browser() afterwards:
  // window creation can be refused (profile shutdown, policy), and a
  // controller without a window is inert.
  MiniWindowController(Profile* profile, const GURL& url);
  MiniWindowController(const MiniWindowController&) = delete;
  MiniWindowController& operator=(const MiniWindowController&) = delete;
  ~MiniWindowController();

  // The window, or null once it has been asked to close.  Never a window that
  // is already scheduled for deletion, so a caller cannot navigate or reuse a
  // window that is on its way out.
  BrowserWindowInterface* browser() const;

  Profile* profile() const { return profile_; }

  // The Space whose identity this window's tab is pinned to.  Fixed for the
  // window's lifetime: the tab's storage partition is chosen when the tab is
  // created and preserved across every navigation it makes afterwards.
  const std::string& space_id() const { return space_id_; }

  // What the window is showing -- the URL a Space would receive.
  GURL current_url() const;

  content::WebContents* web_contents() const;

  // Points the window at |url|, replacing whatever it was showing.
  void NavigateTo(const GURL& url);

  void ShowAndFocus();

  // Opens current_url() as a new tab in |space_id| and closes this window.
  void SendToSpace(const std::string& space_id);

  // Asks the window to close.  The mini window stops being the open one
  // immediately; the window itself goes away once it has run its unload
  // handlers, and this controller with it.
  void Close();

  // Transitional, for the web-modal browser tests written against the
  // pre-Browser mini window.  Checkpoint 5 retires both.
  content::WebContents* web_contents_for_testing() { return web_contents(); }

 private:
  void OnBrowserDidClose(BrowserWindowInterface* browser);

  // Drops everything published on the window's host and stops observing it.
  // Called while the window is still alive, so no registration outlives the
  // host it was made on.
  void Detach();

  const raw_ptr<Profile> profile_;

  // Cleared the moment the window is closing -- before the window is actually
  // gone -- so nothing here can hand out a browser that is mid-teardown.
  raw_ptr<BrowserWindowInterface> browser_ = nullptr;

  // The Space identity of the window, and the two objects that publish it.
  // Both have to be in place before the first navigation: a new tab's storage
  // partition is resolved from AvoraWindowSessionData (see
  // GetSiteInstanceForNewAvoraTab), so a tab created before this is published
  // lands in whichever Space happened to be active globally.
  std::string space_id_;
  std::unique_ptr<WindowSpaceState> space_state_;
  std::optional<ui::ScopedUnownedUserData<WindowSpaceState>> space_state_holder_;
  AvoraWindowSessionData session_data_;
  std::optional<ui::ScopedUnownedUserData<AvoraWindowSessionData>>
      session_data_holder_;
  MiniWindowState mini_window_state_;
  std::optional<ui::ScopedUnownedUserData<MiniWindowState>>
      mini_window_state_holder_;

  base::CallbackListSubscription browser_close_subscription_;

  bool closing_ = false;
};

// Transitional alias and accessor for the browser tests written against the
// pre-Browser mini window, which reached the page through the contents view.
// Checkpoint 5 retires both along with those tests' workaround.
using MiniWindowView = MiniWindowController;
MiniWindowView* GetMiniWindowViewForTesting();

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_MINI_WINDOW_H_
