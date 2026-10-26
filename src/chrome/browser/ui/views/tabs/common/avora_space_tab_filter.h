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
#include "base/memory/weak_ptr.h"
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
//
// A Space's daily list is also one list across every window of the profile.
// A WebContents can only live in one tab strip, so each window holds its own
// copy of every daily tab, and the copies of one logical tab share its Avora
// tab GUID.  Opening, closing, moving to another Space, and navigating are
// carried to the other windows' copies (see FlushDailyTabSync()).  A copy made
// for another window is unloaded -- no renderer, nothing fetched -- until the
// user selects it there.
class AvoraSpaceTabFilter : public TabStripModelObserver,
                            public SpaceManagerObserver,
                            public SidebarItemStore::Observer,
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

  // Gives Avora's native pin gesture -- the tab context menu's "Pin Tab",
  // its keyboard shortcut, anything else that ends in
  // TabStripModel::SetTabsPinned() -- the same persistence the drag gesture
  // already gets (see tab_drag_handler.cc): a backing kPinned item on pin,
  // and its removal on unpin.  Without this, a natively pinned tab is
  // nothing but a bit on the tab strip: it vanishes on restart, and
  // AvoraPinnedSectionView renders it beside, rather than instead of, the
  // persisted item for the same URL.
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;

  // Re-runs the daily-list visibility rules immediately.
  //
  // ApplyVisibility() normally rides on a TabStripModel or Space
  // notification.  A tab's kPinned marker is WebContents user data, though,
  // and setting it (MarkPinnedItemTab) raises nothing at all -- so a caller
  // that marks or unmarks a tab outside those events has to say so here, or
  // the tab keeps whatever visibility it had until some unrelated event
  // happens to re-apply the rules.  Dropping a tab onto a pinned folder is
  // exactly that case: it marks the tab without pinning it natively, so no
  // OnTabPinnedStateChanged() follows (see tab_drag_handler.cc).
  void RefreshTabVisibility();

  // Re-runs tagging and the visibility rules when any tab gains or loses a
  // link preview.  A preview's own tab is hidden from the daily list and
  // untagged; promoting it reverses both.
  void OnLinkPreviewStateChanged(content::WebContents* host_tab);

  // SidebarItemStore::Observer:
  //
  // Removing a kPinned record (UnpinAndRemovePinnedItem) unmarks the tab, and
  // an unmarked tab belongs back in the daily list.  When the tab was never
  // natively pinned -- anything filed into a folder by drag -- there is no
  // SetTabPinned() to unwind, so no OnTabPinnedStateChanged() announces it
  // and the tab would stay hidden: gone from the pinned section and still
  // missing from the daily list, with its page open the whole time.
  void OnSidebarItemsChanged() override;

  // SpaceManagerObserver:
  void OnSpacesChanged() override;
  void OnActiveSpaceChanged(const std::string& space_id) override;

  // WindowSpaceState::Observer:
  void OnWindowActiveSpaceChanged(const std::string& space_id) override;

  // Fills a brand-new, still-empty window with the daily tabs its Space has
  // open in the user's other windows, and selects the one the user was last
  // on.  Returns true when the window now has tabs, in which case the caller
  // must not add the usual blank New Tab: doing so would mirror an empty tab
  // into every other window of the Space.
  bool CarryOverDailyTabs();

 private:
  TabStripModel* GetModel() const;

  // Caches each Space's owning BrowserProfile so OnSpacesChanged() can tell an
  // identity change apart from a rename, reorder, or icon edit.
  void RefreshSpaceProfileCache();

  // Records |space_id| as this window's active Space on AvoraWindowSessionData,
  // which is what new-tab partition selection reads.  Called before any path
  // that can create a tab, so the tab's identity does not depend on which
  // WindowSpaceState observer happens to run first.
  void PublishWindowActiveSpace(const std::string& space_id);

  // Rebuilds every tab belonging to |space_id| in this window so it runs in
  // the Space's current storage partition.
  //
  // A tab's partition is fixed for the life of its WebContents, so switching
  // identity means replacing the contents: for each tab we create a fresh
  // WebContents pinned to the new partition, load the URL the tab was showing,
  // and swap it in with TabStripModel::DiscardWebContents.  That path fires
  // the discard callback, which carries the Avora tab GUID, Space tag, and
  // Favorite marker across to the replacement.
  //
  // Live page state in those tabs (form input, scroll offset, media playback)
  // is necessarily lost, because the old renderer cannot be reused in a
  // different partition.
  //
  // Returns false if any tab could not be rebuilt, in which case the caller
  // must keep treating the Space as still owing a migration.
  bool MigrateSpaceTabsToNewIdentity(const std::string& space_id);

  // Assigns any tab with no Space to the active one.  Runs on every tab strip
  // change rather than only on insertion, so tabs that arrive by drag, restore,
  // or undo are covered too.
  void AdoptUntaggedTabs();

  // Picks up a Space reassignment made outside this class.  "Move to Space"
  // (the tab context menu, a pinned row's menu) is expressed purely as a
  // retag of the WebContents plus a tab-change notification, which keeps
  // those call sites free of any dependency on the window's filter; this is
  // where that retag lands in |tab_space_|, and where a pinned tab's backing
  // kPinned item is carried across with it.  Returns true when |tab| had in
  // fact moved, in which case the caller owes a store sync and a visibility
  // pass.
  bool AdoptRetaggedTab(tabs::TabInterface* tab);

  // Drops entries whose tab has gone away.
  void PruneClosedTabs();

  // Records the active tab as the current Space's tab.
  void RememberActiveTab();

  // Activates the remembered tab for |space_id|, else the first tab belonging
  // to it, else opens a fresh tab so the Space is never empty on entry.
  void RestoreActiveTabFor(const std::string& space_id);

  // Pulls the selection back into the active Space when a close has pushed it
  // out.
  //
  // Every Space's tabs share the one TabStripModel, so the tab Chromium
  // selects after a close -- its neighbour by strip index -- can just as
  // easily belong to another Space.  That tab's row is hidden here, so the
  // window ends up showing a page with nothing selected beside it, and the
  // page is one the user filed under a different Space.  Re-selects the
  // nearest tab that does belong, and falls back to RestoreActiveTabFor()
  // when the close emptied the Space.
  void EnsureActiveTabInCurrentSpace();

  // Shows tabs in the active Space and hides the rest.
  void ApplyVisibility();

  // Writes this window's tab set to SidebarItemStore as kTabState items keyed
  // by Avora tab GUID, so Space and Favorite ownership outlives the process.
  //
  // Merges rather than replaces: kTabState is a profile-wide record set and
  // this window sees only its own strip, so records it does not speak for --
  // another window's tabs, and its own tabs that session restore has not
  // finished delivering -- are carried through untouched.  Rewriting the
  // whole set from one window's partial view is what used to drop a tab's
  // Space on the next launch and let it be re-adopted by whichever Space
  // happened to be showing.
  void SyncTabsToStore();

  // Notes that |tab| is being closed for good, so SyncTabsToStore() stops
  // carrying its record forward.  Not called when the whole window is going
  // away: those tabs come back on the next launch and must keep their Space.
  void ForgetClosedTab(tabs::TabInterface* tab);

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

  // Reconnects restored tabs to the pinned items (SidebarItemType::kPinned)
  // they materialize, mirroring AdoptFavoriteTabs() exactly: the mark lives
  // on the WebContents and is gone after a restart, so the reconnect is made
  // by (url, Space) for restored tabs that are not yet marked.
  void AdoptPinnedItemTabs();

  // One-time migration: gives every tab currently pinned via the native
  // TabStripModel pinned bit a backing kPinned SidebarItem, so pinning that
  // predates this feature is not silently lost. Guarded by
  // kNativePinnedTabsBackfilledPref so it runs at most once per profile;
  // safe to call from every window's constructor because
  // PinnedItemsManager::AddPinnedItem() is itself a no-op for a URL already
  // pinned in that Space, so a race between two windows on first launch
  // cannot create duplicates.
  void BackfillNativePinnedTabs();

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

  // ── Cross-window daily tab sync ──
  //
  // Every change is recorded as it happens and carried to the other windows
  // later, from a posted task: TabStripModel CHECKs against being modified
  // while it is notifying its observers, and a change in one window would
  // otherwise be applied to another window's strip from inside a notification
  // that could, through that window's own observers, lead straight back here.

  // The filters of the other normal windows of this profile.
  std::vector<AvoraSpaceTabFilter*> SyncPeers() const;

  // Whether this window shares daily tabs at all.  Popups, app windows, and
  // the mini window keep their tabs to themselves.
  bool ParticipatesInDailyTabSync() const;

  // Whether |tab| is in the daily list, as opposed to being a Favorite's,
  // a pinned item's, a natively pinned, or a link preview's tab.
  bool IsDailyTab(tabs::TabInterface* tab) const;

  // Whether |tab| has anything to copy yet: a GUID and a committed entry.  A
  // tab is announced to the other windows only once it does.
  bool CanCopyDailyTab(tabs::TabInterface* tab) const;

  // The tab in this window carrying |guid|, of any kind, or null.
  tabs::TabInterface* FindTabByGuid(const std::string& guid) const;

  // Whether this window holds, or has shown, |space_id|'s daily list, and so
  // keeps a copy of every tab in it.
  bool HasJoinedSpace(const std::string& space_id) const;

  // Posts FlushDailyTabSync() unless it is already pending.
  void ScheduleDailyTabSync();

  // Announces this window's new daily tabs to the other windows, then the
  // ones that left the daily list, then the navigations of the rest.
  void FlushDailyTabSync();

  // Gives this window a copy of every daily tab |space_id| has open in the
  // other windows, in their order.  Returns this window's copy of the tab the
  // most recently used other window has selected, when that tab is one of
  // |space_id|'s daily tabs.
  tabs::TabInterface* JoinSpace(const std::string& space_id);

  // Inserts an unloaded copy of |source| at |index| as |space_id|'s tab
  // |guid|.  The copy carries |source|'s whole back/forward history, and
  // stays unloaded unless |activate| puts it on screen.
  tabs::TabInterface* InsertDailyTabCopy(content::WebContents* source,
                                         const std::string& space_id,
                                         const std::string& guid,
                                         int index,
                                         bool activate = false);

  // Builds the unloaded WebContents InsertDailyTabCopy() and
  // ApplyRemoteNavigation() put in the strip.
  std::unique_ptr<content::WebContents> CreateDailyTabCopy(
      content::WebContents* source,
      const std::string& space_id,
      const std::string& guid);

  // Closes this window's copy of |guid| without treating it as the user
  // closing it, which would forget the tab's persisted state and announce the
  // close all over again.
  void CloseDailyTabCopy(tabs::TabInterface* tab);

  // Applied to this window when another window announces a change.
  void ApplyRemoteUpsert(tabs::TabInterface* source,
                         const std::string& guid,
                         const std::string& space_id,
                         const std::string& predecessor_guid,
                         const std::string& successor_guid);
  void ApplyRemoteRemove(const std::string& guid);
  void ApplyRemoteNavigation(tabs::TabInterface* source,
                             const std::string& guid);

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<TabStripView> tab_strip_view_;
  raw_ptr<WindowSpaceState> window_space_state_ = nullptr;
  std::unique_ptr<SpaceManager> space_manager_;
  std::unique_ptr<SidebarItemStore> item_store_;

  // Owning Space per tab.
  std::map<tabs::TabHandle, std::string> tab_space_;

  // space_id -> profile_id, as of the last time the Space list was seen.
  std::map<std::string, std::string> space_profile_;

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

  // GUIDs of tabs this window closed outright.  Their kTabState records are
  // dropped on the next sync instead of being preserved as another window's.
  std::set<std::string> forgotten_tab_guids_;

  std::set<tabs::TabHandle> discard_observed_tabs_;
  std::map<tabs::TabHandle, base::CallbackListSubscription>
      discard_subscriptions_;

  base::RepeatingTimer today_tab_sweep_timer_;
  base::CallbackListSubscription session_restore_subscription_;

  // Fires when any tab in the profile gains or loses a link preview; a
  // preview's own tab is hidden from the daily list, so both edges change
  // what this filter should be drawing.
  base::CallbackListSubscription link_preview_subscription_;

  // The daily tabs the other windows have been told about, with the GUID and
  // Space they were told.  A tab missing from here, or listed under another
  // GUID or Space, is announced on the next flush.
  struct PublishedTab {
    std::string guid;
    std::string space_id;
  };
  std::map<tabs::TabHandle, PublishedTab> published_tabs_;

  // GUIDs of published daily tabs the user closed here since the last flush.
  std::set<std::string> pending_removed_guids_;

  // Published daily tabs that have changed since the last flush, whose copies
  // may have to follow them to a new page.
  std::set<tabs::TabHandle> navigated_tabs_;

  // Spaces whose daily list this window has shown.  See HasJoinedSpace().
  std::set<std::string> joined_spaces_;

  // GUIDs whose copies this window is closing on another window's say-so.
  // Not forgotten: the tab lives on elsewhere, and may still be persisted by
  // the window that owns it.
  std::set<std::string> remote_closing_guids_;

  // Set until this window has been given its Space's daily tabs.  A window
  // made by Cmd+N gets them from CarryOverDailyTabs() before its first tab;
  // any other new window -- a tab dragged out, a link opened in a new window
  // -- gets them on the first flush, behind the tab it was opened for.
  bool pending_initial_join_ = false;

  bool sync_flush_scheduled_ = false;

  base::WeakPtrFactory<AvoraSpaceTabFilter> weak_factory_{this};

  friend bool CarryDailyTabsIntoNewWindow(BrowserWindowInterface* browser);
};

// Gives |browser|, a window just created for Cmd+N, its Space's daily tabs
// from the user's other windows.  Returns true when it now has tabs.  See
// AvoraSpaceTabFilter::CarryOverDailyTabs().
bool CarryDailyTabsIntoNewWindow(BrowserWindowInterface* browser);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_TABS_COMMON_AVORA_SPACE_TAB_FILTER_H_
