// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_link_preview_tab_state.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/avora/avora_sidebar_item.h"
#include "chrome/browser/avora/avora_tab_site_instance.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_favorite_tab_marker.h"
#include "chrome/browser/ui/views/avora/avora_link_preview_tab_marker.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_tab_marker.h"
#include "chrome/browser/ui/views/avora/avora_side_by_side.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "url/origin.h"

namespace avora {

namespace {

const char kLinkPreviewTabKey[] = "avora_link_preview_tab";

using StateChangedCallbackList =
    base::RepeatingCallbackList<void(content::WebContents*)>;

StateChangedCallbackList& GetStateChangedCallbacks() {
  static base::NoDestructor<StateChangedCallbackList> callbacks;
  return *callbacks;
}

void NotifyStateChanged(content::WebContents* tab) {
  GetStateChangedCallbacks().Notify(tab);
}

// The tab strip |tab| belongs to, or null when it belongs to none.
TabStripModel* ModelForTab(tabs::TabInterface* tab) {
  BrowserWindowInterface* browser =
      tab ? tab->GetBrowserWindowInterface() : nullptr;
  return browser ? browser->GetTabStripModel() : nullptr;
}

void ClosePreviewTab(tabs::TabHandle handle) {
  tabs::TabInterface* tab = handle.Get();
  TabStripModel* model = ModelForTab(tab);
  if (!model) {
    return;
  }
  const int index = model->GetIndexOfTab(tab);
  if (index == TabStripModel::kNoTab) {
    return;
  }
  // Not CLOSE_CREATE_HISTORICAL_TAB: a dismissed preview is not something the
  // user should be able to reopen from the recently-closed list, and closing
  // the host tab would otherwise leave two entries there for one gesture.
  model->CloseWebContentsAt(index, TabCloseTypes::CLOSE_NONE);
}

// Closing always goes through a posted task.  The common trigger is the host
// tab's WebContents being destroyed, which runs this object's destructor from
// inside TabStripModel's own tab-removal machinery; closing a second tab there
// re-enters the model and trips its ReentrancyCheck.  A handle is exactly the
// right thing to carry across the post: it resolves to null rather than
// dangling if the window took the preview down in the meantime.
void ClosePreviewTabSoon(tabs::TabHandle handle) {
  if (!handle.Get()) {
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ClosePreviewTab, handle));
}

}  // namespace

LinkPreviewTabState::LinkPreviewTabState(tabs::TabHandle preview,
                                         const GURL& url)
    : preview_(preview), url_(url) {}

LinkPreviewTabState::~LinkPreviewTabState() {
  ClosePreviewTabSoon(preview_);
}

tabs::TabHandle LinkPreviewTabState::TakePreview() {
  tabs::TabHandle preview = preview_;
  preview_ = tabs::TabHandle();
  return preview;
}

content::WebContents* LinkPreviewTabState::preview_contents() const {
  tabs::TabInterface* tab = preview_.Get();
  return tab ? tab->GetContents() : nullptr;
}

// static
LinkPreviewTabState* LinkPreviewTabState::Get(content::WebContents* tab) {
  if (!tab) {
    return nullptr;
  }
  return static_cast<LinkPreviewTabState*>(
      tab->GetUserData(kLinkPreviewTabKey));
}

// static
std::unique_ptr<content::WebContents> LinkPreviewTabState::Set(
    content::WebContents* tab,
    std::unique_ptr<content::WebContents> preview,
    const GURL& url) {
  if (!tab || !preview) {
    return preview;
  }
  tabs::TabInterface* host = tabs::TabInterface::MaybeGetFromContents(tab);
  TabStripModel* model = ModelForTab(host);
  if (!model) {
    return preview;
  }

  // Marked before insertion so AvoraSpaceTabFilter hides the tab on the first
  // layout pass it triggers, rather than showing it for a frame.
  content::WebContents* raw_preview = preview.get();
  MarkLinkPreviewTab(raw_preview, host->GetHandle());

  // Immediately after its host, so promoting the preview later lands it where
  // the user would expect a link opened from that tab to be.  ADD_NONE keeps
  // it in the background: the overlay, not the tab strip, is what shows it.
  const int host_index = model->GetIndexOfWebContents(tab);
  const int index =
      host_index == TabStripModel::kNoTab
          ? model->count()
          : InsertionIndexOutsideSplit(*model, host_index + 1);
  model->InsertWebContentsAt(index, std::move(preview), AddTabTypes::ADD_NONE);

  // Insertion transferred ownership to the tab strip, so there is nothing
  // left to hand back even if the lookup below somehow fails.
  tabs::TabInterface* inserted =
      tabs::TabInterface::MaybeGetFromContents(raw_preview);
  if (!inserted) {
    return nullptr;
  }

  // Replaces -- and so closes -- whatever preview the tab was already
  // carrying, by way of the outgoing state's destructor.
  tab->SetUserData(
      kLinkPreviewTabKey,
      std::make_unique<LinkPreviewTabState>(inserted->GetHandle(), url));
  NotifyStateChanged(tab);
  return nullptr;
}

// static
void LinkPreviewTabState::Clear(content::WebContents* tab) {
  LinkPreviewTabState* state = Get(tab);
  if (!state) {
    return;
  }
  // Taken first so removing the user data does not also queue a close; the
  // close below is the only one.
  const tabs::TabHandle preview = state->TakePreview();
  tab->RemoveUserData(kLinkPreviewTabKey);
  ClosePreviewTabSoon(preview);
  NotifyStateChanged(tab);
}

// static
content::WebContents* LinkPreviewTabState::Promote(content::WebContents* tab) {
  LinkPreviewTabState* state = Get(tab);
  if (!state) {
    return nullptr;
  }
  const tabs::TabHandle preview = state->TakePreview();
  tab->RemoveUserData(kLinkPreviewTabKey);

  tabs::TabInterface* preview_tab = preview.Get();
  content::WebContents* contents =
      preview_tab ? preview_tab->GetContents() : nullptr;
  // Losing the mark is what lets the tab strip start drawing it: from here on
  // it is an ordinary tab that AvoraSpaceTabFilter will tag into a Space.
  UnmarkLinkPreviewTab(contents);
  NotifyStateChanged(tab);
  return contents;
}

bool TabHasLinkPreview(content::WebContents* tab) {
  // Keyed on a live preview rather than on the state object: if the preview
  // tab is closed by some route other than Clear() -- the tab context menu,
  // a "close other tabs", anything that reaches the strip directly -- the
  // state lingers with a dead handle, and treating that as "still has a
  // preview" would block the host from ever opening another one.
  return GetLinkPreviewContents(tab) != nullptr;
}

content::WebContents* GetLinkPreviewContents(content::WebContents* tab) {
  LinkPreviewTabState* state = LinkPreviewTabState::Get(tab);
  return state ? state->preview_contents() : nullptr;
}

GURL GetLinkPreviewUrl(content::WebContents* tab) {
  LinkPreviewTabState* state = LinkPreviewTabState::Get(tab);
  return state ? state->previewed_url() : GURL();
}

bool IsLinkPreviewReturnToHost(content::WebContents* host,
                               content::WebContents* preview) {
  if (!host || !preview || host == preview ||
      GetLinkPreviewContents(host) != preview) {
    return false;
  }
  content::NavigationEntry* const landed =
      preview->GetController().GetLastCommittedEntry();
  if (!landed || landed->IsInitialEntry() ||
      !landed->GetURL().SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  const GURL opened = GetLinkPreviewUrl(host);
  if (!opened.SchemeIsHTTPOrHTTPS()) {
    return false;
  }

  std::vector<url::Origin> host_origins;
  host_origins.push_back(host->GetPrimaryMainFrame()->GetLastCommittedOrigin());
  if (content::NavigationEntry* const entry =
          host->GetController().GetLastCommittedEntry()) {
    // Only the start of the redirect chain: the hops in between are often
    // the identity provider itself, which is exactly where the preview opens.
    host_origins.push_back(url::Origin::Create(entry->GetOriginalRequestURL()));
  }
  std::string item_id = GetFavoriteIdForTab(host);
  if (item_id.empty()) {
    item_id = GetPinnedItemIdForTab(host);
  }
  if (!item_id.empty()) {
    SidebarItemStore store(
        Profile::FromBrowserContext(host->GetBrowserContext())->GetPrefs());
    if (const SidebarItem* item = store.GetItemById(item_id)) {
      host_origins.push_back(url::Origin::Create(GURL(item->url)));
    }
  }

  auto belongs_to_host = [&](const GURL& url) {
    return std::ranges::any_of(host_origins, [&](const url::Origin& origin) {
      return origin.IsSameOriginWith(url);
    });
  };
  return !belongs_to_host(opened) && belongs_to_host(landed->GetURL());
}

std::unique_ptr<content::WebContents> CreateLinkPreviewContents(
    BrowserWindowInterface* browser,
    const GURL& url) {
  if (!browser) {
    return nullptr;
  }
  Profile* profile = browser->GetProfile();
  return content::WebContents::Create(content::WebContents::CreateParams(
      profile, GetSiteInstanceForNewAvoraTab(browser, url)));
}

base::CallbackListSubscription AddLinkPreviewStateChangedCallback(
    base::RepeatingCallback<void(content::WebContents*)> callback) {
  return GetStateChangedCallbacks().Add(std::move(callback));
}

void NotifyLinkPreviewStateChanged(content::WebContents* tab) {
  NotifyStateChanged(tab);
}

}  // namespace avora
