// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_TABS_COMMON_AVORA_SPACE_TAB_FILTER_H_
#define CHROME_BROWSER_UI_VIEWS_TABS_COMMON_AVORA_SPACE_TAB_FILTER_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/avora/avora_sidebar_item.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/tabs/public/tab_interface.h"

class BrowserWindowInterface;
class Profile;
class TabStripModel;
class TabStripView;

namespace content {
class WebContents;
}

namespace avora {

// Makes the daily tab list Space-scoped.
//
// Every Space's tabs stay resident in the one TabStripModel; switching Spaces
// only changes which tab views are visible.  Nothing is detached or destroyed,
// so a switch is instant and preserves scroll position, form state, and media
// playback.
//
// Tabs are keyed by tabs::TabHandle rather than WebContents because a
// background tab's WebContents can be swapped out when it is discarded to save
// memory, which would lose a tag attached to the contents.  Durable identity
// across restarts is the Avora tab GUID persisted in session metadata and in
// SidebarItemStore kTabState records.
//
// Each Space also remembers the tab that was last active in it, so returning to
// a Space restores the page the user left rather than dumping them on an
// arbitrary tab.
class AvoraSpaceTabFilter : public TabStripModelObserver,
                            public SpaceManagerObserver,
                            public WindowSpaceState::Observer {
 public:
  AvoraSpaceTabFilter(BrowserWindowInterface* browser,
                      TabStripView* tab_strip_view,
                      WindowSpaceState* window_space_state);
  AvoraSpaceTabFilter(const AvoraSpaceTabFilter&) = delete;
  AvoraSpaceTabFilter& operator=(const AvoraSpaceTabFilter&) = delete;
  ~AvoraSpaceTabFilter() override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;

  // SpaceManagerObserver:
  void OnActiveSpaceChanged(const std::string& space_id) override;

  // WindowSpaceState::Observer:
  void OnWindowActiveSpaceChanged(const std::string& space_id) override;

 private:
  TabStripModel* GetModel() const;

  // Assigns any tab with no Space to the active one.  Runs on every tab strip
  // change rather than only on insertion, so tabs that arrive by drag, restore,
  // or undo are covered too.
  void AdoptUntaggedTabs();

  // Drops entries whose tab has gone away.
  void PruneClosedTabs();

  // Records the active tab as the current Space's tab.
  void RememberActiveTab();

  // Activates the remembered tab for |space_id|, else the first tab belonging
  // to it, else opens a fresh tab so the Space is never empty on entry.
  void RestoreActiveTabFor(const std::string& space_id);

  // Shows tabs in the active Space and hides the rest.
  void ApplyVisibility();

  // Writes the current tab set to SidebarItemStore as kTabState items keyed by
  // Avora tab GUID, so Space and Favorite ownership outlives the process.
  void SyncTabsToStore();

  // Loads persisted kTabState claims once.
  void EnsureTabStateClaimsLoaded();

  // Reassigns restored tabs by Avora tab GUID before any URL matching runs.
  void RestoreTagsFromGuid();

  // Loads persisted kToday claims once; each (url, space) pair is consumed at
  // most once across repeated calls.  Legacy sessions only.
  void EnsureTodayClaimsLoaded();

  // Reassigns restored tabs to the Space they were in last session by matching
  // persisted kToday items on URL.  Legacy fallback when no Avora GUID exists.
  void RestoreTagsFromStore();

  // Re-runs store restore, favorite adoption, and default adoption for tabs
  // whose URL was not yet knowable on insertion.
  void ResolvePendingTabAssignments(bool sync_store);

  // Reconnects restored tabs to the Favorites that own them, so clicking a
  // Favorite resolves to an existing tab instead of creating a throwaway one.
  //
  // The in-memory mark lives on the WebContents and is gone after a restart,
  // so the reconnect is made by URL for legacy sessions only.
  void AdoptFavoriteTabs();

  // Removes stale kToday records per |avora.today_tab_expiry_hours|.  Skipped
  // entirely when expiry is disabled (0 hours).
  void SweepExpiredTodayTabsIfEnabled();

  // (space_id, url) keys for open daily tabs that must survive a sweep.
  std::set<std::pair<std::string, std::string>> CollectProtectedTodayTabs()
      const;

  void OnSessionRestored(Profile* profile, int num_tabs);

  void EnsureDiscardObserver(tabs::TabInterface* tab);
  void OnTabDiscarded(tabs::TabInterface* tab,
                      content::WebContents* old_contents,
                      content::WebContents* new_contents);
  void AssignTabFromPersistedState(tabs::TabInterface* tab,
                                   const SidebarItem& state,
                                   TabStripModel* model,
                                   int index,
                                   std::set<std::string>* claimed_favorites);
  void MirrorTabGuidOntoContents(tabs::TabInterface* tab);

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<TabStripView> tab_strip_view_;
  raw_ptr<WindowSpaceState> window_space_state_ = nullptr;
  std::unique_ptr<SpaceManager> space_manager_;
  std::unique_ptr<SidebarItemStore> item_store_;

  // Owning Space per tab.
  std::map<tabs::TabHandle, std::string> tab_space_;

  // Last active tab per Space.
  std::map<std::string, tabs::TabHandle> last_active_tab_;

  std::string current_space_id_;

  // Guards against reentrancy while we mutate visibility or the tab strip.
  bool applying_ = false;

  // Remaining kToday (url -> space_ids) claims from the last session.
  std::map<std::string, std::vector<std::string>> today_claims_by_url_;
  bool today_claims_loaded_ = false;

  // GUID -> persisted tab state loaded from SidebarItemStore.
  std::map<std::string, SidebarItem> tab_state_claims_by_guid_;
  bool tab_state_claims_loaded_ = false;

  // GUIDs already matched to a live tab this session.
  std::set<std::string> claimed_tab_guids_;

  std::set<tabs::TabHandle> discard_observed_tabs_;
  std::map<tabs::TabHandle, base::CallbackListSubscription>
      discard_subscriptions_;

  base::RepeatingTimer today_tab_sweep_timer_;
  base::CallbackListSubscription session_restore_subscription_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_TABS_COMMON_AVORA_SPACE_TAB_FILTER_H_
