// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_TAB_SYNC_WATCHER_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_TAB_SYNC_WATCHER_H_

#include <optional>
#include <set>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/avora/sync/avora_tab_snapshot.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"

class Profile;

namespace avora {

// Every open tab of `profile` that tab sync should consider, across all of its
// tabbed windows and Spaces, most recently active window first and in tab
// strip order within a window. Link previews are left out; filtering by URL
// and folding the per-window copies of a daily tab happen in
// sync::BuildTabsList(). Null while the browser is quitting (see
// sync::TabSyncService::TabCollector).
std::optional<std::vector<sync::OpenTab>> CollectOpenTabs(Profile* profile);

// Tells each profile's TabSyncService when its tabs change: a tab added,
// closed, moved, navigated, retitled, pinned or unpinned, or activated, in any
// tabbed window. One process-wide instance, started from BrowserView; it
// watches window creation itself, so windows opened later are covered.
class TabSyncWatcher : public BrowserCollectionObserver,
                       public TabStripModelObserver {
 public:
  // Starts the watcher once and gives TabSyncService its tab collector.
  // Later calls do nothing.
  static void EnsureStarted();

  TabSyncWatcher(const TabSyncWatcher&) = delete;
  TabSyncWatcher& operator=(const TabSyncWatcher&) = delete;

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override;
  void OnBrowserClosed(BrowserWindowInterface* browser) override;
  void OnBrowserActivated(BrowserWindowInterface* browser) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

 private:
  TabSyncWatcher();
  ~TabSyncWatcher() override;

  void Watch(BrowserWindowInterface* browser);
  void NotifyChanged(Profile* profile);

  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browsers_observation_{this};
  std::set<raw_ptr<TabStripModel>> watched_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_TAB_SYNC_WATCHER_H_
