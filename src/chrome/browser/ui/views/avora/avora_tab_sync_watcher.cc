// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_tab_sync_watcher.h"

#include <map>
#include <string>

#include "base/functional/bind.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_tab_guid.h"
#include "chrome/browser/avora/avora_tab_space.h"
#include "chrome/browser/avora/sync/avora_tab_sync_service.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_change_type.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_link_preview_tab_marker.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_tab_marker.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"

namespace avora {

namespace {

// Only ordinary tabbed windows: popups (including the mini window), app and
// DevTools windows are not tabs the user would look for on their phone.
bool IsWatchedBrowser(BrowserWindowInterface* browser) {
  return browser && browser->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
         browser->GetProfile() && !browser->GetProfile()->IsOffTheRecord();
}

bool IsQuitting() {
  return browser_shutdown::IsTryingToQuit() ||
         browser_shutdown::HasShutdownStarted();
}

GURL UrlOf(content::WebContents* contents) {
  // A tab mid-navigation still shows its committed page; a copy that has
  // never loaded has only the entry it was created with.
  GURL url = contents->GetLastCommittedURL();
  return url.is_empty() ? contents->GetVisibleURL() : url;
}

}  // namespace

std::optional<std::vector<sync::OpenTab>> CollectOpenTabs(Profile* profile) {
  if (IsQuitting()) {
    return std::nullopt;
  }
  std::vector<sync::OpenTab> tabs;
  if (!profile) {
    return tabs;
  }
  std::map<std::string, std::string> space_names;
  for (const Space& space : SpaceManager(profile->GetPrefs()).GetSpaces()) {
    space_names[space.id] = space.name;
  }

  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* browser) {
        if (!IsWatchedBrowser(browser) || browser->GetProfile() != profile) {
          return true;
        }
        TabStripModel* model = browser->GetTabStripModel();
        for (int i = 0; model && i < model->count(); ++i) {
          tabs::TabInterface* tab = model->GetTabAtIndex(i);
          content::WebContents* contents = tab ? tab->GetContents() : nullptr;
          if (!contents || IsLinkPreviewTab(contents)) {
            continue;
          }
          sync::OpenTab open_tab;
          open_tab.url = UrlOf(contents);
          open_tab.title = contents->GetTitle();
          // A natively pinned tab (Favorites ride on that bit too) or the tab
          // behind a sidebar pinned item.
          open_tab.pinned = tab->IsPinned() || IsPinnedItemTab(contents);
          if (auto it = space_names.find(GetTabSpaceId(contents));
              it != space_names.end()) {
            open_tab.space = it->second;
          }
          open_tab.last_active = contents->GetLastActiveTime();
          open_tab.guid = GetTabGuid(contents);
          tabs.push_back(std::move(open_tab));
        }
        return true;
      });
  return tabs;
}

// static
void TabSyncWatcher::EnsureStarted() {
  static TabSyncWatcher* instance = nullptr;
  if (instance) {
    return;
  }
  // Never destroyed: it watches for the life of the browser process, and the
  // collections it observes are torn down at shutdown without it.
  instance = new TabSyncWatcher();
}

TabSyncWatcher::TabSyncWatcher() {
  sync::TabSyncService::SetTabCollector(base::BindRepeating(&CollectOpenTabs));
  browsers_observation_.Observe(GlobalBrowserCollection::GetInstance());
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [this](BrowserWindowInterface* browser) {
        Watch(browser);
        return true;
      });
}

TabSyncWatcher::~TabSyncWatcher() = default;

void TabSyncWatcher::OnBrowserCreated(BrowserWindowInterface* browser) {
  Watch(browser);
}

void TabSyncWatcher::OnBrowserClosed(BrowserWindowInterface* browser) {
  if (!IsWatchedBrowser(browser)) {
    return;
  }
  if (TabStripModel* model = browser->GetTabStripModel();
      model && watched_.erase(model)) {
    model->RemoveObserver(this);
  }
  NotifyChanged(browser->GetProfile());
}

void TabSyncWatcher::OnBrowserActivated(BrowserWindowInterface* browser) {
  // The front window's tabs become the most recently active ones. A snapshot
  // identical to the last upload is not sent, so this costs nothing when
  // nothing moved.
  if (IsWatchedBrowser(browser)) {
    NotifyChanged(browser->GetProfile());
  }
}

void TabSyncWatcher::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::kSelectionOnly &&
      !selection.active_tab_changed()) {
    return;
  }
  NotifyChanged(tab_strip_model->profile());
}

void TabSyncWatcher::OnTabChangedAt(tabs::TabInterface* tab,
                                    TabChangeType change_type) {
  // kAll is what URL and title changes report; the other kinds are loading
  // spinners, attention dots and the like.
  if (change_type != TabChangeType::kAll || !tab || !tab->GetContents()) {
    return;
  }
  NotifyChanged(
      Profile::FromBrowserContext(tab->GetContents()->GetBrowserContext()));
}

void TabSyncWatcher::OnTabPinnedStateChanged(tabs::TabInterface* tab,
                                             int index) {
  if (tab && tab->GetContents()) {
    NotifyChanged(
        Profile::FromBrowserContext(tab->GetContents()->GetBrowserContext()));
  }
}

void TabSyncWatcher::OnTabStripModelDestroyed(TabStripModel* tab_strip_model) {
  watched_.erase(tab_strip_model);
}

void TabSyncWatcher::Watch(BrowserWindowInterface* browser) {
  if (!IsWatchedBrowser(browser)) {
    return;
  }
  TabStripModel* model = browser->GetTabStripModel();
  if (!model || !watched_.insert(model).second) {
    return;
  }
  model->AddObserver(this);
  // Creates the profile's service, which uploads once its session is read.
  NotifyChanged(browser->GetProfile());
}

void TabSyncWatcher::NotifyChanged(Profile* profile) {
  if (IsQuitting()) {
    return;
  }
  if (sync::TabSyncService* service =
          sync::TabSyncService::GetForProfile(profile)) {
    service->OnTabsChanged();
  }
}

}  // namespace avora
