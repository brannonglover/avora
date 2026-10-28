// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_TAB_SYNC_WATCHER_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_TAB_SYNC_WATCHER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/avora/sync/avora_tab_sync_service.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/tabs/common/avora_space_tab_filter.h"

class Profile;
class TabStripModel;

namespace content {
class WebContents;
}  // namespace content

namespace avora {

// Connects each profile's TabSyncService to the tab strips, which
// //chrome/browser/avora can't reach: today tabs the user opens, navigates or
// closes (reported by AvoraSpaceTabFilter) go to the service, and the
// service's remote changes go to the filters. Also creates a profile's
// service when its first window opens, so a stored session starts syncing,
// fetches other devices' changes whenever a window comes to the front, and
// captures previews of the tabs the user looks at (docs/SYNC.md): a JPEG of
// each window's front tab a moment after it finishes loading, unless the page
// has a password field or was served with Cache-Control: no-store.
//
// One process-wide instance, started from BrowserView.
class TabSyncWatcher : public BrowserCollectionObserver,
                       public TabStripModelObserver,
                       public AvoraSpaceTabFilter::SharedTabsDelegate,
                       public sync::TabSyncService::TodayTabs {
 public:
  // Starts the watcher once. Later calls do nothing.
  static void EnsureStarted();

  TabSyncWatcher(const TabSyncWatcher&) = delete;
  TabSyncWatcher& operator=(const TabSyncWatcher&) = delete;

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override;
  void OnBrowserActivated(BrowserWindowInterface* browser) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

  // AvoraSpaceTabFilter::SharedTabsDelegate:
  void OnSharedTabOpened(Profile* profile,
                         const AvoraSpaceTabFilter::SharedTab& tab,
                         const std::string& predecessor,
                         const std::string& successor) override;
  void OnSharedTabNavigated(Profile* profile,
                            const AvoraSpaceTabFilter::SharedTab& tab) override;
  void OnSharedTabClosed(Profile* profile, const std::string& guid) override;
  std::vector<AvoraSpaceTabFilter::SharedTab> SharedTabsForSpace(
      Profile* profile,
      const std::string& space_id) override;

  // sync::TabSyncService::TodayTabs:
  bool IsReady(Profile* profile) override;
  std::vector<sync::TabSyncService::TodayTab> Collect(Profile* profile) override;
  void Upsert(Profile* profile,
              const sync::TabSyncService::TodayTab& tab,
              const std::vector<std::string>& before) override;
  void Remove(Profile* profile, const std::string& guid) override;
  void RemovePinnedItem(Profile* profile, const std::string& item_id) override;

 private:
  class FrontTab;

  TabSyncWatcher();
  ~TabSyncWatcher() override;

  // Follows `model`'s active tab for previews.
  void WatchFrontTab(TabStripModel* model);
  void CapturePreview(base::WeakPtr<content::WebContents> contents);

  // Each window's front tab, by tab strip.
  std::map<TabStripModel*, std::unique_ptr<FrontTab>> front_tabs_;

  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browsers_observation_{this};
  base::WeakPtrFactory<TabSyncWatcher> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_TAB_SYNC_WATCHER_H_
