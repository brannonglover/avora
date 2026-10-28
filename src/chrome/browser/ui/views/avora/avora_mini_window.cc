// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_mini_window.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_tab_space.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/base/window_open_disposition.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace avora {

namespace {

// How far each open mini window pushes a new one down and to the right.
constexpr int kMiniWindowCascadeOffset = 28;

// The open mini windows, oldest first.  A controller leaves this list the
// moment its window is asked to close, not when the window is finally gone, so
// nothing listed here is on its way out.
std::vector<MiniWindowController*>& OpenMiniWindows() {
  static base::NoDestructor<std::vector<MiniWindowController*>> windows;
  return *windows;
}

void Unregister(MiniWindowController* controller) {
  std::erase(OpenMiniWindows(), controller);
}

gfx::Rect InitialBounds() {
  gfx::Rect bounds(0, 0, kMiniWindowDefaultWidth, kMiniWindowDefaultHeight);
  display::Screen* screen = display::Screen::Get();
  if (!screen) {
    return bounds;
  }
  // On the display the user is actually looking at, which for a link handed
  // over by another app is the one that app was on.
  const gfx::Rect work_area =
      screen->GetDisplayNearestPoint(screen->GetCursorScreenPoint())
          .work_area();
  bounds.set_size(gfx::Size(std::min(bounds.width(), work_area.width()),
                            std::min(bounds.height(), work_area.height())));
  // Slightly above centre: a tall window centred exactly reads as low.
  bounds.set_origin(
      gfx::Point(work_area.x() + (work_area.width() - bounds.width()) / 2,
                 work_area.y() + (work_area.height() - bounds.height()) / 3));
  // Cascaded past the mini windows already open, so a second link visibly
  // lands in a second window rather than exactly on top of the first.
  const int cascade =
      kMiniWindowCascadeOffset * static_cast<int>(OpenMiniWindows().size());
  bounds.Offset(cascade, cascade);
  bounds.AdjustToFit(work_area);
  return bounds;
}

}  // namespace

// ── MiniWindowState ─────────────────────────────────────────────────────────

DEFINE_USER_DATA(MiniWindowState);

MiniWindowState::MiniWindowState() = default;
MiniWindowState::~MiniWindowState() = default;

// ── Public entry points ─────────────────────────────────────────────────────

void ShowMiniWindow(Profile* profile, const GURL& url) {
  if (!profile || !url.is_valid()) {
    return;
  }

  // Always a new window, never the one already on screen: that one is showing
  // a page the user has not decided about yet.
  auto* controller = new MiniWindowController(profile, url);
  if (!controller->browser()) {
    // Window creation was refused.  Nothing to show and nothing to own it.
    delete controller;
    return;
  }
  controller->ShowAndFocus();
}

bool IsMiniWindowShowing() {
  return GetMiniWindowBrowser() != nullptr;
}

void CloseMiniWindow() {
  // Copied first: each Close() takes its controller off the list.
  const std::vector<MiniWindowController*> open = OpenMiniWindows();
  for (MiniWindowController* controller : open) {
    controller->Close();
  }
}

BrowserWindowInterface* GetMiniWindowBrowser() {
  MiniWindowController* open = MiniWindowController::Get();
  return open ? open->browser() : nullptr;
}

bool IsMiniWindow(const BrowserWindowInterface* browser) {
  if (!browser) {
    return false;
  }
  return MiniWindowState::Get(browser->GetUnownedUserDataHost()) != nullptr;
}

std::string PreferredMiniWindowSpaceId(Profile* profile) {
  if (!profile || !profile->GetPrefs()) {
    return std::string();
  }
  // The window the user was last in is the likeliest home for the link, so its
  // Space -- not the profile-wide one -- is what the mini window adopts.
  // FindTabbedBrowser skips popup windows, so an open mini window never
  // answers this question about itself.
  if (BrowserWindowInterface* browser =
          ProfileBrowserCollection::GetForProfile(profile)
              ->FindTabbedBrowser()) {
    if (WindowSpaceState* state = GetWindowSpaceState(browser);
        state && !state->active_space_id().empty()) {
      return state->active_space_id();
    }
  }
  SpaceManager manager(profile->GetPrefs());
  const Space* active = manager.GetActiveSpace();
  return active ? active->id : std::string();
}

std::vector<Space> OrderMiniWindowSpaces(const std::vector<Space>& spaces,
                                         const std::string& preferred) {
  std::vector<Space> ordered;
  ordered.reserve(spaces.size());
  for (const Space& space : spaces) {
    if (!preferred.empty() && space.id == preferred) {
      ordered.push_back(space);
    }
  }
  for (const Space& space : spaces) {
    if (preferred.empty() || space.id != preferred) {
      ordered.push_back(space);
    }
  }
  return ordered;
}

std::vector<Space> MiniWindowOrderedSpaces(
    const BrowserWindowInterface* browser) {
  MiniWindowController* open = MiniWindowController::FromBrowser(browser);
  if (!open || !open->profile() || !open->profile()->GetPrefs()) {
    return {};
  }
  SpaceManager manager(open->profile()->GetPrefs());
  return OrderMiniWindowSpaces(manager.GetSpaces(), open->space_id());
}

void SendMiniWindowToSpace(const BrowserWindowInterface* browser,
                           const std::string& space_id) {
  if (MiniWindowController* open = MiniWindowController::FromBrowser(browser)) {
    open->SendToSpace(space_id);
  }
}

MiniWindowView* GetMiniWindowViewForTesting() {
  return MiniWindowController::Get();
}

// ── MiniWindowController ────────────────────────────────────────────────────

// static
MiniWindowController* MiniWindowController::Get() {
  const std::vector<MiniWindowController*>& open = OpenMiniWindows();
  return open.empty() ? nullptr : open.back();
}

// static
MiniWindowController* MiniWindowController::FromBrowser(
    const BrowserWindowInterface* browser) {
  if (!browser) {
    return nullptr;
  }
  for (MiniWindowController* controller : OpenMiniWindows()) {
    if (controller->browser() == browser) {
      return controller;
    }
  }
  return nullptr;
}

MiniWindowController::MiniWindowController(Profile* profile, const GURL& url)
    : profile_(profile) {
  BrowserWindowCreateParams params(BrowserWindowInterface::TYPE_POPUP, profile,
                                   /*from_user_gesture=*/true);
  params.initial_bounds = InitialBounds();
  // Those bounds are chosen for the display the link came from, so Chromium
  // should not second-guess the origin.
  params.initial_origin_specified =
      BrowserWindowCreateParams::ValueSpecified::kSpecified;
  params.can_maximize = false;
  // Builds the mini window's own chrome in place of the standard popup
  // location bar.  Read during BrowserView construction, so it has to travel
  // in the create params.
  params.avora_mini_window = true;
  // A glance is not part of the session.  Restoring a mini window would put a
  // window on screen for a link the user never asked to keep, and opening one
  // is not the start of a session either.
  params.omit_from_session_restore = true;
  params.should_trigger_session_restore = false;

  BrowserWindowInterface* browser = CreateBrowserWindow(std::move(params));
  if (!browser) {
    return;
  }
  browser_ = browser;
  // The one notification that is guaranteed to arrive while the window is
  // still alive.  Everything this controller published on the window has to
  // come back off before the window it was published on is destroyed.
  browser_close_subscription_ = browser->RegisterBrowserDidClose(
      base::BindRepeating(&MiniWindowController::OnBrowserDidClose,
                          base::Unretained(this)));

  // Identity before the page.  A new tab's storage partition is resolved from
  // the window's Space (GetSiteInstanceForNewAvoraTab reads it off
  // AvoraWindowSessionData), so publishing this after the first navigation
  // would pin the tab to whichever Space happened to be active globally and
  // leave the window showing the link as the wrong identity.
  space_state_ = std::make_unique<WindowSpaceState>(profile->GetPrefs());
  const std::string preferred = PreferredMiniWindowSpaceId(profile);
  if (!preferred.empty()) {
    space_state_->SetActiveSpaceId(preferred);
  }
  // Read back rather than assuming: a Space that has since been deleted falls
  // back to the global default inside WindowSpaceState.
  space_id_ = space_state_->active_space_id();
  space_state_holder_.emplace(browser->GetUnownedUserDataHost(),
                              *space_state_);
  session_data_.set_window_guid(space_state_->window_guid());
  session_data_.set_active_space_id(space_id_);
  session_data_holder_.emplace(browser->GetUnownedUserDataHost(),
                               session_data_);
  mini_window_state_holder_.emplace(browser->GetUnownedUserDataHost(),
                                    mini_window_state_);

  OpenMiniWindows().push_back(this);
  NavigateTo(url);
}

MiniWindowController::~MiniWindowController() {
  Unregister(this);
  // Nothing may still be registered on a window that is being destroyed; by
  // the time this runs OnBrowserDidClose has already unpublished it, and this
  // is only a backstop for a controller that never got a window.
  Detach();
}

BrowserWindowInterface* MiniWindowController::browser() const {
  if (!browser_ || browser_->IsDeleteScheduled()) {
    return nullptr;
  }
  return browser_;
}

content::WebContents* MiniWindowController::web_contents() const {
  BrowserWindowInterface* browser = this->browser();
  return browser ? browser->GetTabStripModel()->GetActiveWebContents()
                 : nullptr;
}

GURL MiniWindowController::current_url() const {
  content::WebContents* contents = web_contents();
  return contents ? contents->GetVisibleURL() : GURL();
}

void MiniWindowController::NavigateTo(const GURL& url) {
  BrowserWindowInterface* browser = this->browser();
  if (!browser || !url.is_valid()) {
    return;
  }
  NavigateParams params(browser, url, ui::PAGE_TRANSITION_LINK);
  // The first navigation creates the window's one tab; every later one
  // replaces the page in it.  A popup's strip accepts a tab only while it is
  // empty (WindowCanOpenTabs), so asking for a new tab afterwards would hand
  // the navigation to a tabbed window -- the right answer for a link the page
  // opens, the wrong one for the mini window being re-pointed.
  params.disposition = browser->GetTabStripModel()->empty()
                           ? WindowOpenDisposition::NEW_FOREGROUND_TAB
                           : WindowOpenDisposition::CURRENT_TAB;
  Navigate(&params);
}

void MiniWindowController::ShowAndFocus() {
  if (BrowserWindowInterface* browser = this->browser()) {
    browser->GetWindow()->Show();
    browser->GetWindow()->Activate();
  }
}

void MiniWindowController::SendToSpace(const std::string& space_id) {
  const GURL url = current_url();
  if (!url.is_valid() || !profile_) {
    return;
  }

  BrowserWindowInterface* target =
      ProfileBrowserCollection::GetForProfile(profile_)->FindTabbedBrowser();
  if (!target) {
    // Nothing to receive the tab: a mini window can be the only Avora window
    // on screen, and this is the moment the user asked for a real one.
    target = CreateBrowserWindow(
        BrowserWindowCreateParams(profile_.get(), /*from_user_gesture=*/true));
    if (!target) {
      return;
    }
    target->GetWindow()->Show();
  }

  // Point the window at the Space before navigating.  A new tab's storage
  // partition is resolved from the window's Space, so switching afterwards
  // would put the tab in the wrong identity -- and the user would land on a
  // Space that isn't showing their new tab.
  if (!space_id.empty()) {
    if (WindowSpaceState* state = GetWindowSpaceState(target)) {
      state->SetActiveSpaceId(space_id);
    }
  }

  NavigateParams params(target, url, ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
  if (params.navigated_or_inserted_contents && !space_id.empty()) {
    // Tag it so the Space's daily list claims the tab even if the window is
    // switched away before the load finishes.
    SetTabSpaceId(params.navigated_or_inserted_contents, space_id);
  }

  target->GetWindow()->Activate();
  Close();
}

void MiniWindowController::Close() {
  if (closing_) {
    return;
  }
  closing_ = true;
  // No longer an open mini window, from this moment: the window may take a
  // trip through its unload handlers before it goes, and a window on its way
  // out must not be handed out.
  Unregister(this);
  if (browser_) {
    browser_->GetWindow()->Close();
  }
}

void MiniWindowController::OnBrowserDidClose(BrowserWindowInterface* browser) {
  closing_ = true;
  Unregister(this);
  Detach();
  // The window is gone; so is the reason this exists.  Deleted on the next
  // turn of the loop rather than here, because this runs from the window's own
  // close notification.
  base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                                this);
}

void MiniWindowController::Detach() {
  mini_window_state_holder_.reset();
  session_data_holder_.reset();
  space_state_holder_.reset();
  browser_close_subscription_ = base::CallbackListSubscription();
  browser_ = nullptr;
}

}  // namespace avora
