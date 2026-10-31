// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/tabs/common/avora_space_tab_filter.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <utility>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "chrome/browser/avora/avora_pinned_folders.h"
#include "chrome/browser/avora/avora_pinned_items.h"
#include "chrome/browser/avora/avora_prefs.h"
#include "chrome/browser/avora/avora_tab_guid.h"
#include "chrome/browser/avora/avora_tab_site_instance.h"
#include "chrome/browser/avora/avora_tab_space.h"
#include "chrome/browser/avora/avora_window_session_data.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/tab_list/tab_removed_reason.h"
#include "chrome/browser/ui/views/avora/avora_favorite_tab_marker.h"
#include "chrome/browser/ui/views/avora/avora_link_preview_tab_marker.h"
#include "chrome/browser/ui/views/avora/avora_link_preview_tab_state.h"
#include "chrome/browser/ui/views/avora/avora_live_folder_tab_marker.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_materializer.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_tab_marker.h"
#include "chrome/browser/ui/views/avora/avora_side_by_side.h"
#include "components/prefs/pref_service.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/browser/ui/views/tabs/common/split_tab_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/common/pinned_tab_container_view.h"
#include "chrome/browser/ui/views/tabs/common/unpinned_tab_container_view.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/tabs/public/split_tab_data.h"
#include "content/public/browser/invalidate_type.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/restore_type.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "ui/base/page_transition_types.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace avora {

namespace {

// Every live filter, so a window can reach the other windows' daily lists.
// Filters live and die on the UI thread with their window.
std::vector<AvoraSpaceTabFilter*>& DailyTabSyncRegistry() {
  static base::NoDestructor<std::vector<AvoraSpaceTabFilter*>> registry;
  return *registry;
}

// Tabs dragged out of one window and not yet claimed by the one they were
// dropped in.  Lets the receiving window tell a moved tab, which takes over
// from its own copy, apart from a duplicated one, which is a new tab.
std::set<tabs::TabHandle>& DailyTabsInTransit() {
  static base::NoDestructor<std::set<tabs::TabHandle>> in_transit;
  return *in_transit;
}

// Background daily tabs a window keeps loaded; switching back to one of them
// is instant, and the rest wait unloaded (each costs a renderer otherwise).
constexpr size_t kWarmTabBudget = 12;

// The one shared-tabs delegate (the views layer's tab sync bridge), or null.
AvoraSpaceTabFilter::SharedTabsDelegate*& SharedTabsDelegateInstance() {
  static AvoraSpaceTabFilter::SharedTabsDelegate* delegate = nullptr;
  return delegate;
}

}  // namespace

AvoraSpaceTabFilter::AvoraSpaceTabFilter(BrowserWindowInterface* browser,
                                         TabStripView* tab_strip_view,
                                         WindowSpaceState* window_space_state)
    : browser_(browser),
      tab_strip_view_(tab_strip_view),
      window_space_state_(window_space_state) {
  if (!browser_) {
    return;
  }

  if (Profile* profile = browser_->GetProfile()) {
    space_manager_ = std::make_unique<SpaceManager>(profile->GetPrefs());
    space_manager_->AddObserver(this);

    // Seed from the per-window state when available; fall back to the global
    // pref for backward compatibility.
    if (window_space_state_) {
      current_space_id_ = window_space_state_->active_space_id();
      window_space_state_->AddObserver(this);
    } else if (const Space* active = space_manager_->GetActiveSpace()) {
      current_space_id_ = active->id;
    }
    item_store_ = std::make_unique<SidebarItemStore>(profile->GetPrefs());
    item_store_->AddObserver(this);

    // Baseline for identity-change detection in OnSpacesChanged().
    RefreshSpaceProfileCache();
  }

  if (TabStripModel* model = GetModel()) {
    model->AddObserver(this);
  }

  // Opening a preview hides a tab and promoting one reveals it, and neither
  // is guaranteed to be followed by a TabStripModel notification this object
  // would otherwise see in the right order.
  link_preview_subscription_ = AddLinkPreviewStateChangedCallback(
      base::BindRepeating(&AvoraSpaceTabFilter::OnLinkPreviewStateChanged,
                          base::Unretained(this)));
  live_folder_tab_subscription_ = AddLiveFolderTabMarkChangedCallback(
      base::BindRepeating(&AvoraSpaceTabFilter::OnLiveFolderTabMarkChanged,
                          base::Unretained(this)));

  // Order matters: recover last session's GUID assignments before URL fallback,
  // both before AdoptUntaggedTabs(), and the pinned-item backfill last so it
  // only ever creates an item for a tab nothing above already claimed.
  RestoreTagsFromGuid();
  RestoreTagsFromStore();
  AdoptFavoriteTabs();
  AdoptPinnedItemTabs();
  AdoptUntaggedTabs();
  BackfillNativePinnedTabs();
  RememberActiveTab();
  ApplyVisibility();

  // Sweep after RestoreTagsFromStore() has consumed kToday records for tabs
  // already in the strip.  Defer while session restore is still inserting tabs,
  // or stale records for not-yet-restored URLs could be removed before they
  // can be matched.
  Profile* profile = browser_->GetProfile();
  if (profile && SessionRestore::IsRestoring(profile)) {
    session_restore_subscription_ =
        SessionRestore::RegisterOnSessionRestoredCallback(base::BindRepeating(
            &AvoraSpaceTabFilter::OnSessionRestored, base::Unretained(this)));
  } else {
    SweepExpiredTodayTabsIfEnabled();
  }

  if (profile) {
    const base::TimeDelta expiry = GetTodayTabExpiry(profile->GetPrefs());
    if (expiry.is_positive()) {
      today_tab_sweep_timer_.Start(
          FROM_HERE, base::Minutes(30),
          base::BindRepeating(
              &AvoraSpaceTabFilter::SweepExpiredTodayTabsIfEnabled,
              base::Unretained(this)));
    }
  }

  DailyTabSyncRegistry().push_back(this);
  if (ParticipatesInDailyTabSync()) {
    pending_initial_join_ = true;
    ScheduleDailyTabSync();
  }
}

AvoraSpaceTabFilter::~AvoraSpaceTabFilter() {
  std::erase(DailyTabSyncRegistry(), this);
  if (window_space_state_) {
    window_space_state_->RemoveObserver(this);
  }
  if (space_manager_) {
    space_manager_->RemoveObserver(this);
  }
  if (item_store_) {
    item_store_->RemoveObserver(this);
  }
  if (TabStripModel* model = GetModel()) {
    model->RemoveObserver(this);
  }
}

TabStripModel* AvoraSpaceTabFilter::GetModel() const {
  return browser_ ? browser_->GetTabStripModel() : nullptr;
}

namespace {

// The URL a tab should be remembered by.
GURL UrlForTab(const tabs::TabInterface* tab) {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!contents) {
    return GURL();
  }
  GURL url = contents->GetLastCommittedURL();
  return url.is_empty() ? contents->GetVisibleURL() : url;
}

bool IsUsableTabUrl(const GURL& url) {
  return url.is_valid() && !url.is_empty() && !url.IsAboutBlank();
}

// A restored tab can exist in the strip with no committed URL yet.  Leave it
// untagged until its destination is knowable; fresh about:blank tabs from
// Cmd+T or RestoreActiveTabFor must still be claimed immediately.
bool ShouldDeferSpaceAssignment(const tabs::TabInterface* tab) {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!contents) {
    return false;
  }

  content::NavigationController& controller = contents->GetController();
  if (controller.IsInitialBlankNavigation()) {
    return false;
  }

  if (IsUsableTabUrl(UrlForTab(tab))) {
    return false;
  }

  // Cloned/restored tabs stay in limbo until their session URL commits.
  if (controller.IsInitialNavigation()) {
    return true;
  }

  return controller.GetEntryCount() > 0 || controller.GetPendingEntry();
}

}  // namespace

// ── Tab strip changes ───────────────────────────────────────────────────────

void AvoraSpaceTabFilter::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // Even a change this filter made itself can leave a tab to announce, such as
  // the blank tab RestoreActiveTabFor() opens in an empty Space.
  ScheduleDailyTabSync();

  if (applying_) {
    return;
  }

  // A tab the user closed should stop being remembered; a tab going away
  // because its window is closing should not, since session restore brings it
  // back and it has to land in the Space it left.
  const bool tabs_removed = change.type() == TabStripModelChange::kRemoved &&
                            !tab_strip_model->closing_all();
  if (tabs_removed) {
    for (const auto& removed : change.GetRemove()->contents) {
      if (TabRemoveReasonUtils::WillDeleteTab(removed.remove_reason)) {
        ForgetClosedTab(removed.tab);
      }
    }
  }

  // The same distinction decides what the other windows' copies do.  A tab
  // closed here closes everywhere.  A tab dragged to another window is still
  // open, so its copies stay; the window it lands in sorts out its own copy.
  // A window closing takes nothing with it from the others.
  //
  // Not only published tabs: a tab can already have copies elsewhere before
  // this window has announced it -- a new window copies the list as it finds
  // it -- so any daily tab's close goes out under its GUID.
  if (change.type() == TabStripModelChange::kRemoved) {
    for (const auto& removed : change.GetRemove()->contents) {
      const tabs::TabHandle handle = removed.tab->GetHandle();
      const auto published = published_tabs_.find(handle);
      std::string guid;
      if (published != published_tabs_.end()) {
        guid = published->second.guid;
        published_tabs_.erase(published);
      } else if (IsDailyTab(removed.tab)) {
        guid = GetTabGuid(removed.tab->GetContents());
      }
      if (guid.empty() || tab_strip_model->closing_all()) {
        continue;
      }
      if (TabRemoveReasonUtils::WillDeleteTab(removed.remove_reason)) {
        pending_removed_guids_.insert(guid);
      } else if (removed.remove_reason ==
                 TabRemovedReason::kInsertedIntoOtherTabStrip) {
        DailyTabsInTransit().insert(handle);
      }
    }
  }

  PruneClosedTabs();
  TabStripModel* model = GetModel();
  if (model) {
    for (int i = 0; i < model->count(); ++i) {
      EnsureDiscardObserver(model->GetTabAtIndex(i));
    }
  }
  // Before AdoptUntaggedTabs: session restore delivers tabs in batches after
  // construction, and each batch is a chance to reconnect a Favorite.
  ResolvePendingTabAssignments(/*sync_store=*/true);

  // Chromium picks the post-close selection by strip index, which knows
  // nothing about Spaces.  Correct it before RememberActiveTab() reads the
  // active tab, so a foreign tab never gets recorded as this Space's own.
  if (tabs_removed) {
    EnsureActiveTabInCurrentSpace();
  }

  if (selection.active_tab_changed()) {
    RememberActiveTab();
  }

  ApplyVisibility();
}

void AvoraSpaceTabFilter::OnTabChangedAt(tabs::TabInterface* tab,
                                           TabChangeType change_type) {
  if (applying_ || !tab || change_type != TabChangeType::kAll) {
    return;
  }

  // kAll covers a commit and a title change, either of which the tab's copies
  // in other windows may have to follow.
  if (published_tabs_.contains(tab->GetHandle())) {
    navigated_tabs_.insert(tab->GetHandle());
  }
  ScheduleDailyTabSync();

  // A "Move to Space" elsewhere in the UI arrives here, as a retag of the
  // tab's WebContents announced through this notification.
  if (AdoptRetaggedTab(tab)) {
    SyncTabsToStore();
    ApplyVisibility();
    return;
  }

  const tabs::TabHandle handle = tab->GetHandle();
  const bool untagged = tab_space_.find(handle) == tab_space_.end();
  content::WebContents* contents = tab->GetContents();
  const bool may_need_favorite =
      contents && !IsFavoriteTab(contents) && IsUsableTabUrl(UrlForTab(tab));
  if (!untagged && !may_need_favorite) {
    return;
  }

  ResolvePendingTabAssignments(/*sync_store=*/true);
  ApplyVisibility();
}

void AvoraSpaceTabFilter::OnSplitTabChanged(const SplitTabChange& change) {
  // Separating a pair puts its tabs back in the container as ordinary rows,
  // which have to pick up the visibility the pair's row was carrying.
  if (!applying_) {
    ApplyVisibility();
  }
}

void AvoraSpaceTabFilter::OnTabPinnedStateChanged(tabs::TabInterface* tab,
                                                  int /*index*/) {
  if (applying_ || !tab) {
    return;
  }
  content::WebContents* contents = tab->GetContents();
  Profile* profile = browser_ ? browser_->GetProfile() : nullptr;
  PrefService* prefs = profile ? profile->GetPrefs() : nullptr;
  if (!contents || !prefs) {
    return;
  }

  const auto space_it = tab_space_.find(tab->GetHandle());
  const std::string space_id =
      space_it != tab_space_.end() ? space_it->second : current_space_id_;
  if (space_id.empty()) {
    return;
  }

  // Cheap, self-syncing facades over the same prefs every other manager
  // writes to -- the same pattern BackfillNativePinnedTabs() uses, and for
  // the same reason: nothing here needs to outlive this call.
  PinnedItemsManager items_manager(prefs);
  items_manager.SetWindowActiveSpaceId(space_id);

  // A null TabStripModel throughout: the native pinned bit is what got us
  // here, so only the persistence half is owed, and calling SetTabPinned()
  // back into the model from inside its own notification is not safe.
  if (tab->IsPinned()) {
    if (!IsUsableTabUrl(UrlForTab(tab))) {
      return;
    }
    PinAndCreatePinnedItem(/*tab_strip=*/nullptr, &items_manager, contents);
    return;
  }

  PinnedFoldersManager folders_manager(prefs);
  folders_manager.SetWindowActiveSpaceId(space_id);

  if (GetPinnedItemIdForTab(contents).empty()) {
    // Unpinning a tab that carries no marker -- one pinned before this hook
    // existed, or restored without AdoptPinnedItemTabs() reaching it.  The
    // persisted item it stands for still has to go, or the pin the user
    // just removed silently returns on the next restart.
    const std::string item_id =
        items_manager.GetPinnedItemIdForUrl(UrlForTab(tab).spec());
    if (item_id.empty()) {
      return;
    }
    MarkPinnedItemTab(contents, item_id);
  }
  UnpinAndRemovePinnedItem(/*tab_strip=*/nullptr, &items_manager,
                           &folders_manager, contents);
}

void AvoraSpaceTabFilter::OnActiveSpaceChanged(const std::string& space_id) {
  // When per-window state is driving the filter, the global pref-based change
  // is irrelevant for this window.  See OnWindowActiveSpaceChanged() instead.
  if (window_space_state_) {
    return;
  }
  if (space_id == current_space_id_) {
    return;
  }

  // Save where the user was before leaving the outgoing Space.
  RememberActiveTab();

  current_space_id_ = space_id;

  // Keep new-tab partition selection consistent with the Space we are entering
  // before RestoreActiveTabFor() can create one.
  PublishWindowActiveSpace(space_id);

  // Bring in the Space's tabs from the other windows first, so an empty-looking
  // Space is not handed a blank tab it does not need.
  if (tabs::TabInterface* peer_selection = JoinSpace(space_id)) {
    last_active_tab_.try_emplace(space_id, peer_selection->GetHandle());
  }

  // Activate before hiding, so the content area never shows a tab that is
  // about to disappear from the strip.
  RestoreActiveTabFor(space_id);

  AdoptUntaggedTabs();
  ApplyVisibility();
  SyncTabsToStore();
}

void AvoraSpaceTabFilter::OnWindowActiveSpaceChanged(
    const std::string& space_id) {
  if (space_id == current_space_id_) {
    return;
  }

  RememberActiveTab();

  current_space_id_ = space_id;

  // Publish the window's Space before RestoreActiveTabFor(), which creates a
  // tab when the incoming Space is empty.  New-tab partition selection reads
  // this value, so leaving it to another observer would make the new tab's
  // identity depend on observer registration order -- and an empty Space
  // entered first would open its tab in the outgoing Space's partition.
  PublishWindowActiveSpace(space_id);

  // The Space's daily list is the same in every window, so entering it here
  // brings in whatever the other windows have open in it.  Before
  // RestoreActiveTabFor(), which would otherwise open a blank tab in a Space
  // that only looks empty from this window.  A Space never shown here has no
  // remembered tab yet, so it opens on the one the user was last on elsewhere.
  if (tabs::TabInterface* peer_selection = JoinSpace(space_id)) {
    last_active_tab_.try_emplace(space_id, peer_selection->GetHandle());
  }

  RestoreActiveTabFor(space_id);

  AdoptUntaggedTabs();
  ApplyVisibility();
  SyncTabsToStore();
}

void AvoraSpaceTabFilter::PublishWindowActiveSpace(
    const std::string& space_id) {
  if (!browser_) {
    return;
  }
  // Absent on windows without the Avora views; new tabs there fall back to the
  // global active Space.
  if (auto* data =
          AvoraWindowSessionData::Get(browser_->GetUnownedUserDataHost())) {
    data->set_active_space_id(space_id);
  }
}

void AvoraSpaceTabFilter::OnSpacesChanged() {
  if (!space_manager_) {
    return;
  }

  const std::vector<Space> spaces = space_manager_->GetSpaces();

  // Forget Spaces that no longer exist.
  std::set<std::string> live;
  for (const Space& space : spaces) {
    live.insert(space.id);
  }
  for (auto it = space_profile_.begin(); it != space_profile_.end();) {
    it = live.count(it->first) ? std::next(it) : space_profile_.erase(it);
  }

  // A Space we've never seen before is simply recorded: it has no tabs in this
  // window yet, so there is nothing to move.
  std::vector<std::string> reidentified;
  for (const Space& space : spaces) {
    const auto it = space_profile_.find(space.id);
    if (it == space_profile_.end()) {
      space_profile_[space.id] = space.profile_id;
    } else if (it->second != space.profile_id) {
      reidentified.push_back(space.id);
    }
  }

  if (reidentified.empty()) {
    return;
  }

  // Another tab-strip operation is mid-flight, or a migration is already
  // running.  Leave the cached identity stale so this change is still visible
  // as a delta and gets picked up next time, rather than being swallowed.
  if (applying_) {
    return;
  }

  for (const std::string& space_id : reidentified) {
    // Only record the new identity once the tabs have actually been rebuilt in
    // it.  If a rebuild is incomplete the entry stays stale and is retried.
    if (MigrateSpaceTabsToNewIdentity(space_id)) {
      if (const Space* space = space_manager_->GetSpaceById(space_id)) {
        space_profile_[space_id] = space->profile_id;
      }
    }
  }
}

void AvoraSpaceTabFilter::RefreshSpaceProfileCache() {
  if (!space_manager_) {
    return;
  }
  std::map<std::string, std::string> fresh;
  for (const Space& space : space_manager_->GetSpaces()) {
    fresh[space.id] = space.profile_id;
  }
  space_profile_ = std::move(fresh);
}

bool AvoraSpaceTabFilter::MigrateSpaceTabsToNewIdentity(
    const std::string& space_id) {
  TabStripModel* model = GetModel();
  if (!model || !browser_ || space_id.empty()) {
    return false;
  }
  Profile* profile = browser_->GetProfile();
  if (!profile) {
    return false;
  }

  // Collect first: swapping contents mutates the strip, and we don't want to
  // iterate it while it changes.  Whether a tab is the foreground one is
  // recorded up front too, because the active WebContents pointer is destroyed
  // as soon as that tab is swapped and must not be compared against later.
  content::WebContents* const active_contents = model->GetActiveWebContents();
  std::vector<std::pair<tabs::TabHandle, bool>> targets;
  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (!tab) {
      continue;
    }
    const auto it = tab_space_.find(tab->GetHandle());
    if (it != tab_space_.end() && it->second == space_id) {
      targets.emplace_back(tab->GetHandle(),
                           tab->GetContents() == active_contents);
    }
  }
  if (targets.empty()) {
    return true;
  }

  bool migrated_all = true;

  applying_ = true;

  for (const auto& [handle, is_active] : targets) {
    tabs::TabInterface* tab = handle.Get();
    if (!tab) {
      continue;
    }
    content::WebContents* old_contents = tab->GetContents();
    if (!old_contents) {
      migrated_all = false;
      continue;
    }

    // The discard callback is what carries the tab GUID, Space tag, and
    // Favorite marker onto the replacement contents.  Registration normally
    // happens in OnTabStripModelChanged, which bails out while |applying_| is
    // set, so make sure this tab is subscribed before we swap it.
    EnsureDiscardObserver(tab);

    // Reload whatever the tab is showing now, not the URL it started on.
    const GURL url = old_contents->GetLastCommittedURL().is_valid()
                         ? old_contents->GetLastCommittedURL()
                         : old_contents->GetVisibleURL();

    // Capture durable identity while the outgoing contents is still alive;
    // DiscardWebContents destroys it before we can read any of this again.
    // The replacement is a brand-new WebContents with brand-new navigation
    // entries, so nothing here can be recovered from it afterwards.
    std::string tab_guid = GetTabGuid(old_contents);
    if (tab_guid.empty()) {
      tab_guid = FindTabGuidInController(&old_contents->GetController());
    }
    const bool was_favorite = IsFavoriteTab(old_contents);
    const std::string favorite_id =
        was_favorite ? GetFavoriteIdForTab(old_contents) : std::string();
    const bool favorite_was_pinned =
        was_favorite && WasPinnedBeforeFavorite(old_contents);

    scoped_refptr<content::SiteInstance> site_instance =
        GetSiteInstanceForSpace(profile, space_id, url);

    content::WebContents::CreateParams create_params(profile, site_instance);
    // A background tab must stay unrendered until the user activates it.
    create_params.initially_hidden = !is_active;

    std::unique_ptr<content::WebContents> new_contents =
        content::WebContents::Create(create_params);
    if (!new_contents) {
      migrated_all = false;
      continue;
    }

    if (url.is_valid() && !url.IsAboutBlank()) {
      content::NavigationController::LoadURLParams load_params(url);
      load_params.transition_type = ui::PAGE_TRANSITION_AUTO_TOPLEVEL;
      new_contents->GetController().LoadURLWithParams(load_params);
    }

    // Swapping through the model preserves index, pinned state, and group, and
    // fires OnTabDiscarded so the Avora tab GUID, Space tag, and Favorite
    // marker follow the contents.  Holding the outgoing contents in a local
    // keeps it alive until the end of this iteration, after which its renderer
    // in the previous partition is torn down.
    std::unique_ptr<content::WebContents> retired =
        model->DiscardWebContents(old_contents, std::move(new_contents));

    // Re-assert identity on the replacement rather than trusting the discard
    // callback alone.  SetTabGuid also mirrors the GUID onto the pending and
    // committed navigation entries, which is what lets session restore match
    // this tab back to its persisted Space after a restart.
    if (content::WebContents* swapped = tab->GetContents()) {
      SetTabSpaceId(swapped, space_id);
      if (!tab_guid.empty()) {
        SetTabGuid(swapped, tab_guid);
      } else {
        GetOrCreateTabGuid(swapped);
      }
      if (was_favorite && !favorite_id.empty()) {
        MarkFavoriteTab(swapped, favorite_id, favorite_was_pinned);
      }
    }
  }

  applying_ = false;

  // The replacements are new WebContents, so re-apply the tags that hang off
  // contents rather than off the tab, then refresh visibility and persistence.
  AdoptUntaggedTabs();
  ApplyVisibility();
  SyncTabsToStore();

  return migrated_all;
}

void AvoraSpaceTabFilter::EnsureTabStateClaimsLoaded() {
  if (tab_state_claims_loaded_ || !item_store_) {
    return;
  }
  tab_state_claims_loaded_ = true;

  for (const auto& item : item_store_->GetAllItems()) {
    if (item.type == SidebarItemType::kTabState && !item.id.empty()) {
      tab_state_claims_by_guid_[item.id] = item;
    }
  }
}

void AvoraSpaceTabFilter::MirrorTabGuidOntoContents(tabs::TabInterface* tab) {
  if (!tab || !tab->GetContents()) {
    return;
  }
  AdoptTabGuidFromController(tab->GetContents());
  SyncTabGuidToAllEntries(tab->GetContents());
}

void AvoraSpaceTabFilter::AssignTabFromPersistedState(
    tabs::TabInterface* tab,
    const SidebarItem& state,
    TabStripModel* model,
    int index,
    std::set<std::string>* claimed_favorites) {
  if (!tab || !tab->GetContents() || state.space_id.empty()) {
    return;
  }

  content::WebContents* contents = tab->GetContents();
  tab_space_[tab->GetHandle()] = state.space_id;
  SetTabSpaceId(contents, state.space_id);

  if (!state.favorite_id.empty() && claimed_favorites &&
      !claimed_favorites->count(state.favorite_id) &&
      !IsFavoriteTab(contents)) {
    MarkFavoriteTab(contents, state.favorite_id, model->IsTabPinned(index));
    claimed_favorites->insert(state.favorite_id);
  }
}

void AvoraSpaceTabFilter::RestoreTagsFromGuid() {
  TabStripModel* model = GetModel();
  if (!model) {
    return;
  }

  EnsureTabStateClaimsLoaded();
  std::set<std::string> claimed_favorites;

  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (!tab) {
      continue;
    }
    const tabs::TabHandle handle = tab->GetHandle();
    if (tab_space_.find(handle) != tab_space_.end()) {
      continue;
    }

    MirrorTabGuidOntoContents(tab);
    const std::string guid = GetTabGuid(tab->GetContents());
    if (guid.empty() || claimed_tab_guids_.count(guid)) {
      continue;
    }

    const auto it = tab_state_claims_by_guid_.find(guid);
    if (it == tab_state_claims_by_guid_.end()) {
      continue;
    }

    AssignTabFromPersistedState(tab, it->second, model, i, &claimed_favorites);
    claimed_tab_guids_.insert(guid);
    tab_state_claims_by_guid_.erase(it);
  }
}

void AvoraSpaceTabFilter::EnsureDiscardObserver(tabs::TabInterface* tab) {
  if (!tab) {
    return;
  }
  const tabs::TabHandle handle = tab->GetHandle();
  if (discard_observed_tabs_.count(handle)) {
    return;
  }
  discard_observed_tabs_.insert(handle);
  discard_subscriptions_[handle] = tab->RegisterWillDiscardContents(
      base::BindRepeating(&AvoraSpaceTabFilter::OnTabDiscarded,
                          base::Unretained(this)));
}

void AvoraSpaceTabFilter::OnTabDiscarded(
    tabs::TabInterface* tab,
    content::WebContents* old_contents,
    content::WebContents* new_contents) {
  TransferTabGuid(old_contents, new_contents);
  if (tab && new_contents) {
    const auto it = tab_space_.find(tab->GetHandle());
    if (it != tab_space_.end()) {
      SetTabSpaceId(new_contents, it->second);
    }
    if (IsFavoriteTab(old_contents)) {
      MarkFavoriteTab(new_contents, GetFavoriteIdForTab(old_contents),
                      WasPinnedBeforeFavorite(old_contents));
    }
    if (IsPinnedItemTab(old_contents)) {
      MarkPinnedItemTab(new_contents, GetPinnedItemIdForTab(old_contents));
    }
    TransferLiveFolderTabMark(old_contents, new_contents);
  }
}

void AvoraSpaceTabFilter::EnsureTodayClaimsLoaded() {
  if (today_claims_loaded_ || !item_store_) {
    return;
  }
  today_claims_loaded_ = true;

  for (const auto& item : item_store_->GetAllItems()) {
    if (item.type == SidebarItemType::kToday && !item.space_id.empty()) {
      today_claims_by_url_[item.url].push_back(item.space_id);
    }
  }
}

void AvoraSpaceTabFilter::ResolvePendingTabAssignments(bool sync_store) {
  RestoreTagsFromGuid();
  RestoreTagsFromStore();
  AdoptFavoriteTabs();
  AdoptPinnedItemTabs();
  AdoptUntaggedTabs();
  if (sync_store) {
    SyncTabsToStore();
  }
}

void AvoraSpaceTabFilter::AdoptFavoriteTabs() {
  TabStripModel* model = GetModel();
  if (!item_store_ || !model) {
    return;
  }

  // Build a URL → favorite info map that carries each favorite's space_id so
  // we can assign the correct Space and avoid cross-Space mis-matches.
  struct FavoriteInfo {
    std::string id;
    std::string space_id;
  };
  std::multimap<std::string, FavoriteInfo> favorites_by_url;
  std::set<std::string> live_favorites;
  for (const auto& item : item_store_->GetAllItems()) {
    if (item.type == SidebarItemType::kFavorite) {
      live_favorites.insert(item.id);
      favorites_by_url.emplace(item.url,
                               FavoriteInfo{item.id, item.space_id});
    }
  }
  // Legacy kFavoriteTabState URL aliases (written by older versions).
  for (const auto& item : item_store_->GetAllItems()) {
    if (item.type == SidebarItemType::kFavoriteTabState &&
        live_favorites.count(item.id)) {
      favorites_by_url.emplace(item.url,
                               FavoriteInfo{item.id, item.space_id});
    }
  }
  if (favorites_by_url.empty()) {
    return;
  }

  std::set<std::string> claimed;
  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (tab && tab->GetContents() && IsFavoriteTab(tab->GetContents())) {
      claimed.insert(GetFavoriteIdForTab(tab->GetContents()));
    }
  }

  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (!tab) {
      continue;
    }
    content::WebContents* contents = tab->GetContents();
    if (!contents || IsFavoriteTab(contents)) {
      continue;
    }
    MirrorTabGuidOntoContents(tab);
    const GURL url = UrlForTab(tab);
    if (!IsUsableTabUrl(url)) {
      continue;
    }

    const tabs::TabHandle handle = tab->GetHandle();
    const auto space_it = tab_space_.find(handle);
    const bool has_space = space_it != tab_space_.end() &&
                           !space_it->second.empty();
    const std::string& tab_space =
        has_space ? space_it->second : std::string();

    // Find the best matching favorite for this URL.  When the tab already
    // belongs to a Space, only accept a favorite from that same Space so
    // favorites from other Spaces do not leak across.
    auto [range_begin, range_end] =
        favorites_by_url.equal_range(url.spec());
    const FavoriteInfo* best = nullptr;
    for (auto it = range_begin; it != range_end; ++it) {
      if (claimed.count(it->second.id)) {
        continue;
      }
      if (has_space && it->second.space_id == tab_space) {
        best = &it->second;
        break;
      }
      if (!has_space && !best) {
        best = &it->second;
      }
    }
    if (!best) {
      continue;
    }

    // For tabs already tagged to a Space, reject favorites from other Spaces.
    if (has_space && !best->space_id.empty() &&
        tab_space != best->space_id) {
      continue;
    }

    MarkFavoriteTab(contents, best->id, model->IsTabPinned(i));
    claimed.insert(best->id);
    GetOrCreateTabGuid(contents);

    // Assign Space from the favorite itself when the tab is still untagged.
    if (!has_space && !best->space_id.empty()) {
      tab_space_[handle] = best->space_id;
      SetTabSpaceId(contents, best->space_id);
    }
  }
}

void AvoraSpaceTabFilter::AdoptPinnedItemTabs() {
  TabStripModel* model = GetModel();
  if (!item_store_ || !model) {
    return;
  }

  // Build a URL -> pinned item info map, mirroring AdoptFavoriteTabs()'s
  // favorites_by_url exactly. Unlike Favorites there is no legacy alias type
  // to also scan: kPinned has never had a predecessor record.
  struct PinnedItemInfo {
    std::string id;
    std::string space_id;
  };
  std::multimap<std::string, PinnedItemInfo> pinned_items_by_url;
  for (const auto& item : item_store_->GetAllItems()) {
    if (item.type == SidebarItemType::kPinned) {
      pinned_items_by_url.emplace(item.url,
                                  PinnedItemInfo{item.id, item.space_id});
    }
  }
  if (pinned_items_by_url.empty()) {
    return;
  }

  std::set<std::string> claimed;
  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (tab && tab->GetContents() && IsPinnedItemTab(tab->GetContents())) {
      claimed.insert(GetPinnedItemIdForTab(tab->GetContents()));
    }
  }

  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (!tab) {
      continue;
    }
    content::WebContents* contents = tab->GetContents();
    if (!contents || IsPinnedItemTab(contents)) {
      continue;
    }
    MirrorTabGuidOntoContents(tab);
    const GURL url = UrlForTab(tab);
    if (!IsUsableTabUrl(url)) {
      continue;
    }

    const tabs::TabHandle handle = tab->GetHandle();
    const auto space_it = tab_space_.find(handle);
    const bool has_space = space_it != tab_space_.end() &&
                           !space_it->second.empty();
    const std::string& tab_space =
        has_space ? space_it->second : std::string();

    // Find the best matching pinned item for this URL. When the tab already
    // belongs to a Space, only accept an item from that same Space so
    // pinned items from other Spaces do not leak across.
    auto [range_begin, range_end] =
        pinned_items_by_url.equal_range(url.spec());
    const PinnedItemInfo* best = nullptr;
    for (auto it = range_begin; it != range_end; ++it) {
      if (claimed.count(it->second.id)) {
        continue;
      }
      if (has_space && it->second.space_id == tab_space) {
        best = &it->second;
        break;
      }
      if (!has_space && !best) {
        best = &it->second;
      }
    }
    if (!best) {
      continue;
    }

    if (has_space && !best->space_id.empty() &&
        tab_space != best->space_id) {
      continue;
    }

    MarkPinnedItemTab(contents, best->id);
    claimed.insert(best->id);
    GetOrCreateTabGuid(contents);

    if (!has_space && !best->space_id.empty()) {
      tab_space_[handle] = best->space_id;
      SetTabSpaceId(contents, best->space_id);
    }
  }
}

void AvoraSpaceTabFilter::BackfillNativePinnedTabs() {
  Profile* profile = browser_ ? browser_->GetProfile() : nullptr;
  TabStripModel* model = GetModel();
  if (!profile || !model) {
    return;
  }

  PrefService* prefs = profile->GetPrefs();
  if (!prefs ||
      !prefs->FindPreference(
          PinnedItemsManager::kNativePinnedTabsBackfilledPref) ||
      prefs->GetBoolean(PinnedItemsManager::kNativePinnedTabsBackfilledPref)) {
    return;
  }

  // A fresh manager instance is enough: it is a cheap, self-syncing facade
  // over the same SidebarItemStore pref every other manager already writes
  // to, not a resource that needs to outlive this one-time pass.
  PinnedItemsManager items_manager(prefs);

  const int first_unpinned = model->IndexOfFirstNonPinnedTab();
  for (int i = 0; i < first_unpinned; ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    content::WebContents* contents = tab ? tab->GetContents() : nullptr;
    if (!contents || IsPinnedItemTab(contents)) {
      // Already backed -- e.g. AdoptPinnedItemTabs() already claimed it, or
      // another window's backfill pass ran first this session.
      continue;
    }
    const GURL url = UrlForTab(tab);
    if (!IsUsableTabUrl(url)) {
      continue;
    }

    const auto space_it = tab_space_.find(tab->GetHandle());
    const std::string space_id =
        space_it != tab_space_.end() ? space_it->second : current_space_id_;
    if (space_id.empty()) {
      continue;
    }

    items_manager.SetWindowActiveSpaceId(space_id);
    std::string item_id = items_manager.AddPinnedItem(
        url.spec(), base::UTF16ToUTF8(contents->GetTitle()));
    if (item_id.empty()) {
      // AddPinnedItem() no-ops when the URL is already pinned in this Space;
      // reuse that existing id instead of leaving this tab unmarked.
      item_id = items_manager.GetPinnedItemIdForUrl(url.spec());
    }
    if (!item_id.empty()) {
      MarkPinnedItemTab(contents, item_id);
    }
  }

  prefs->SetBoolean(PinnedItemsManager::kNativePinnedTabsBackfilledPref, true);
}

void AvoraSpaceTabFilter::SyncTabsToStore() {
  TabStripModel* model = GetModel();
  if (!item_store_ || !model) {
    return;
  }

  std::vector<SidebarItem> items;
  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (!tab || !tab->GetContents()) {
      continue;
    }

    const auto space_it = tab_space_.find(tab->GetHandle());
    if (space_it == tab_space_.end() || space_it->second.empty()) {
      continue;
    }

    const GURL url = UrlForTab(tab);
    if (!IsUsableTabUrl(url) && !IsFavoriteTab(tab->GetContents())) {
      continue;
    }

    const std::string guid = GetOrCreateTabGuid(tab->GetContents());
    SyncTabGuidToAllEntries(tab->GetContents());
    if (guid.empty()) {
      continue;
    }

    SidebarItem item;
    item.id = guid;
    item.space_id = space_it->second;
    item.url = url.is_valid() ? url.spec() : std::string();
    item.title = base::UTF16ToUTF8(tab->GetContents()->GetTitle());
    item.type = SidebarItemType::kTabState;
    if (IsFavoriteTab(tab->GetContents())) {
      item.favorite_id = GetFavoriteIdForTab(tab->GetContents());
    }
    items.push_back(std::move(item));
  }

  // Records this window just wrote supersede their older selves; everything
  // else in the profile-wide kTabState set belongs to a window or a
  // not-yet-restored tab this one cannot see, and is carried through as it
  // stands.  Only a tab closed here is actually dropped.
  std::set<std::string> written;
  for (const auto& item : items) {
    written.insert(item.id);
    // Reopened (Cmd+Shift+T) after being closed: it is a live tab again, so
    // it stops being something to forget.
    forgotten_tab_guids_.erase(item.id);
  }

  // A carried-forward record is refreshed by whichever window owns its tab on
  // that window's next sync, so anything left un-refreshed for this long has
  // no tab anywhere and no session old enough to restore it either.  Without
  // the bound, every window close would leave its tabs' records behind
  // permanently.
  constexpr base::TimeDelta kCarryForwardLimit = base::Days(30);
  const base::Time now = base::Time::Now();

  std::vector<SidebarItem> merged;
  for (const auto& existing : item_store_->GetAllItems()) {
    if (existing.type != SidebarItemType::kTabState || existing.id.empty()) {
      continue;
    }
    if (written.count(existing.id) || forgotten_tab_guids_.count(existing.id)) {
      continue;
    }
    if (!existing.last_accessed_at.is_null() &&
        now - existing.last_accessed_at > kCarryForwardLimit) {
      continue;
    }
    merged.push_back(existing);
  }
  for (auto& item : items) {
    merged.push_back(std::move(item));
  }

  item_store_->ReplaceItemsOfType(SidebarItemType::kTabState, merged,
                                  /*notify=*/false);
}

void AvoraSpaceTabFilter::ForgetClosedTab(tabs::TabInterface* tab) {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!contents) {
    return;
  }
  const std::string guid = GetTabGuid(contents);
  if (!guid.empty() && !remote_closing_guids_.contains(guid)) {
    forgotten_tab_guids_.insert(guid);
  }
}

void AvoraSpaceTabFilter::RestoreTagsFromStore() {
  TabStripModel* model = GetModel();
  if (!item_store_ || !model) {
    return;
  }

  EnsureTodayClaimsLoaded();
  if (today_claims_by_url_.empty()) {
    return;
  }

  // Stale kToday rows for Favorite URLs are ignored so a prior mistag cannot
  // override FavoriteTabState on the next launch.
  std::set<std::string> favorite_urls;
  for (const auto& item : item_store_->GetAllItems()) {
    if (item.type == SidebarItemType::kFavorite ||
        item.type == SidebarItemType::kFavoriteTabState) {
      favorite_urls.insert(item.url);
    }
  }

  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (!tab) {
      continue;
    }
    const tabs::TabHandle handle = tab->GetHandle();
    if (tab_space_.find(handle) != tab_space_.end()) {
      continue;
    }

    MirrorTabGuidOntoContents(tab);

    const GURL url = UrlForTab(tab);
    if (!IsUsableTabUrl(url) || favorite_urls.count(url.spec())) {
      continue;
    }

    const auto it = today_claims_by_url_.find(url.spec());
    if (it == today_claims_by_url_.end() || it->second.empty()) {
      continue;
    }

    // Consume the match so two tabs on the same URL don't both take it.
    tab_space_[handle] = it->second.front();
    SetTabSpaceId(tab->GetContents(), it->second.front());
    GetOrCreateTabGuid(tab->GetContents());
    it->second.erase(it->second.begin());
    if (it->second.empty()) {
      today_claims_by_url_.erase(it);
    }
  }
}

// ── Bookkeeping ─────────────────────────────────────────────────────────────

void AvoraSpaceTabFilter::AdoptUntaggedTabs() {
  TabStripModel* model = GetModel();
  if (!model || current_space_id_.empty()) {
    return;
  }

  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (!tab) {
      continue;
    }
    const tabs::TabHandle handle = tab->GetHandle();
    if (tab_space_.find(handle) != tab_space_.end()) {
      continue;
    }
    // A link preview is a glance, not a daily tab.  Leaving it untagged is
    // what keeps it out of SyncTabsToStore() and the Today sweep, both of
    // which only ever look at tagged tabs.  It is tagged the moment it is
    // promoted, because promotion drops the mark.
    if (IsLinkPreviewTab(tab->GetContents())) {
      continue;
    }
    if (ShouldDeferSpaceAssignment(tab)) {
      continue;
    }
    tab_space_[handle] = current_space_id_;
    SetTabSpaceId(tab->GetContents(), current_space_id_);
    GetOrCreateTabGuid(tab->GetContents());
  }
}

bool AvoraSpaceTabFilter::AdoptRetaggedTab(tabs::TabInterface* tab) {
  content::WebContents* contents = tab->GetContents();
  if (!contents) {
    return false;
  }

  // An empty tag means "not assigned yet", which is AdoptUntaggedTabs()'
  // business, not a move.
  const std::string tagged = GetTabSpaceId(contents);
  if (tagged.empty()) {
    return false;
  }

  const tabs::TabHandle handle = tab->GetHandle();
  const auto it = tab_space_.find(handle);
  if (it == tab_space_.end() || it->second == tagged) {
    return false;
  }
  const std::string previous = it->second;
  it->second = tagged;

  // A pinned tab's persisted kPinned item has to travel with it, or the Space
  // it left resurrects the item as a tab-less row on the next launch and the
  // Space it joined has a pinned tab backed by nothing.  Doing it here rather
  // than at each menu is what lets those call sites express a move as a retag
  // and nothing more.
  if (Profile* profile = browser_ ? browser_->GetProfile() : nullptr) {
    const std::string item_id = GetPinnedItemIdForTab(contents);
    if (!item_id.empty()) {
      MovePinnedItemToSpace(profile->GetPrefs(), item_id, previous, tagged);
    }
  }

  if (tagged == current_space_id_) {
    return true;
  }

  // The tab just left the Space this window is showing.  Drop it as that
  // Space's remembered tab, and if it is the one on screen, hand focus to a
  // tab that still belongs here -- otherwise the content area would keep
  // rendering a page whose row the strip is about to hide.
  const auto remembered = last_active_tab_.find(current_space_id_);
  if (remembered != last_active_tab_.end() && remembered->second == handle) {
    last_active_tab_.erase(remembered);
  }

  TabStripModel* model = GetModel();
  const int index = model ? model->GetIndexOfTab(tab) : -1;
  if (index >= 0 && index == model->active_index()) {
    RestoreActiveTabFor(current_space_id_);
  }
  return true;
}

void AvoraSpaceTabFilter::PruneClosedTabs() {
  std::erase_if(tab_space_, [](const std::pair<const tabs::TabHandle,
                                               std::string>& entry) {
    return entry.first.Get() == nullptr;
  });
  std::erase_if(last_active_tab_,
                [](const std::pair<const std::string, tabs::TabHandle>& entry) {
                  return entry.second.Get() == nullptr;
                });
  std::erase_if(discard_subscriptions_,
                [](const std::pair<const tabs::TabHandle,
                                   base::CallbackListSubscription>& entry) {
                  return entry.first.Get() == nullptr;
                });
  std::erase_if(discard_observed_tabs_, [](const tabs::TabHandle& handle) {
    return handle.Get() == nullptr;
  });
}

void AvoraSpaceTabFilter::RememberActiveTab() {
  TabStripModel* model = GetModel();
  if (!model || current_space_id_.empty()) {
    return;
  }

  const int index = model->active_index();
  if (index < 0 || index >= model->count()) {
    return;
  }

  tabs::TabInterface* tab = model->GetTabAtIndex(index);
  if (!tab) {
    return;
  }

  // A link preview is the active tab for as long as its overlay is open, but
  // it is a glance rather than the Space's tab.  Recording it would make the
  // Space restore to a preview that has since been closed, and would lose the
  // host the user was actually working in -- so the host stays the remembered
  // tab right through the overlay's lifetime.
  if (IsLinkPreviewTab(tab->GetContents())) {
    return;
  }

  // Don't record a tab that belongs to some other Space; that would happen
  // mid-switch and would corrupt the outgoing Space's memory.
  const auto it = tab_space_.find(tab->GetHandle());
  if (it != tab_space_.end() && it->second != current_space_id_) {
    return;
  }

  last_active_tab_[current_space_id_] = tab->GetHandle();
}

void AvoraSpaceTabFilter::RestoreActiveTabFor(const std::string& space_id) {
  TabStripModel* model = GetModel();
  if (!model) {
    return;
  }

  const auto remembered = last_active_tab_.find(space_id);
  if (remembered != last_active_tab_.end()) {
    if (tabs::TabInterface* tab = remembered->second.Get()) {
      if (model->GetIndexOfTab(tab) >= 0) {
        model->ActivateTab(tab);
        return;
      }
    }
  }

  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (!tab) {
      continue;
    }
    const auto it = tab_space_.find(tab->GetHandle());
    if (it != tab_space_.end() && it->second == space_id) {
      model->ActivateTab(tab);
      return;
    }
  }

  // The Space has no tabs yet, so give it one rather than leaving an empty
  // strip with a hidden tab still showing its page.
  if (TabStripModelDelegate* delegate = model->delegate()) {
    applying_ = true;
    delegate->AddTabAt(GURL(), /*index=*/-1, /*foreground=*/true);
    applying_ = false;
  }
}

void AvoraSpaceTabFilter::EnsureActiveTabInCurrentSpace() {
  TabStripModel* model = GetModel();
  if (!model || current_space_id_.empty() || model->empty()) {
    return;
  }

  const int count = model->count();
  const int active = model->active_index();
  if (active < 0 || active >= count) {
    return;
  }

  const auto is_candidate = [&](int index) {
    tabs::TabInterface* tab = model->GetTabAtIndex(index);
    if (!tab) {
      return false;
    }
    // A link preview can legitimately hold the selection: while its overlay is
    // open it is the page the user is interacting with, and making it the real
    // active tab is what lets extension APIs and Chromium's per-tab UI resolve
    // to the card.  It stops being a valid resting place once it is orphaned
    // -- its host gone, so no overlay is showing it and nothing else will move
    // the selection off it.
    content::WebContents* contents = tab->GetContents();
    if (contents && IsLinkPreviewTab(contents)) {
      content::WebContents* const host = GetLinkPreviewHostTab(contents);
      if (!host || GetLinkPreviewContents(host) != contents) {
        return false;
      }
    }
    // Untagged tabs belong to every Space, so one staying selected is right.
    const auto it = tab_space_.find(tab->GetHandle());
    return it == tab_space_.end() || it->second.empty() ||
           it->second == current_space_id_;
  };

  if (is_candidate(active)) {
    return;
  }

  // Walk outwards from where the selection landed, so the user ends up beside
  // the tab they closed rather than at whichever of this Space's tabs happens
  // to sit first in the strip.
  for (int offset = 1; offset < count; ++offset) {
    for (const int index : {active + offset, active - offset}) {
      if (index >= 0 && index < count && is_candidate(index)) {
        model->ActivateTab(model->GetTabAtIndex(index));
        return;
      }
    }
  }

  // The close emptied this Space; RestoreActiveTabFor() opens it a new tab.
  RestoreActiveTabFor(current_space_id_);
}

// ── Visibility ──────────────────────────────────────────────────────────────

void AvoraSpaceTabFilter::OnLinkPreviewStateChanged(
    content::WebContents* host_tab) {
  // The callback list is profile-wide, so most notifications belong to some
  // other window's strip and there is nothing here to re-draw.
  TabStripModel* model = GetModel();
  if (applying_ || !model || !host_tab ||
      model->GetIndexOfWebContents(host_tab) == TabStripModel::kNoTab) {
    return;
  }
  // Promotion is the edge that needs the tagging pass: the tab stopped being
  // a preview, so AdoptUntaggedTabs() claims it for the current Space instead
  // of leaving it untagged and therefore visible in every Space.  No store
  // sync -- that is a pref write, and the next ordinary tab event will carry
  // the new tag out anyway.
  ResolvePendingTabAssignments(/*sync_store=*/false);
  ApplyVisibility();
}

void AvoraSpaceTabFilter::RefreshTabVisibility() {
  ApplyVisibility();
}

void AvoraSpaceTabFilter::OnLiveFolderTabMarkChanged(
    content::WebContents* contents) {
  // Profile-wide, like the link preview callback: only a tab in this strip
  // changes what this window draws.
  TabStripModel* model = GetModel();
  if (applying_ || !model || !contents ||
      model->GetIndexOfWebContents(contents) == TabStripModel::kNoTab) {
    return;
  }
  ApplyVisibility();
  // A daily tab the other windows were already told about has to leave their
  // daily lists too, and one released back to the daily list has to reach
  // them; the sync pass handles both from IsDailyTab().
  ScheduleDailyTabSync();
}

void AvoraSpaceTabFilter::OnSidebarItemsChanged() {
  // Only the visibility rules are re-run: the store is the source of truth
  // for what is pinned, and nothing here writes back to it, so this cannot
  // feed itself.
  ApplyVisibility();
}

void AvoraSpaceTabFilter::ApplyVisibility() {
  if (!tab_strip_view_ || current_space_id_.empty()) {
    return;
  }

  // Both containers are filtered.  Pinning is a per-Space decision just like
  // any other tab: a tab pinned in one Space must not surface in the others.
  views::View* const pinned_container = tab_strip_view_->GetPinnedTabsContainer();
  views::View* const containers[] = {
      tab_strip_view_->GetUnpinnedTabsContainer(),
      pinned_container,
  };

  applying_ = true;
  for (views::View* container : containers) {
    if (!container) {
      continue;
    }
    // Which container a TabView lives in already tells us whether its tab is
    // natively pinned (TabStripModel::IsTabPinned) -- that's exactly how
    // Chromium partitions the two containers -- so it doubles as the signal
    // for the kPinned-hide rule below without a second model lookup.
    const bool is_pinned_container = (container == pinned_container);
    for (views::View* child : container->children()) {
      if (SplitTabView* split_view = views::AsViewClass<SplitTabView>(child)) {
        ApplyVisibilityToSplit(*split_view);
        continue;
      }

      TabView* tab_view = views::AsViewClass<TabView>(child);
      if (!tab_view) {
        // Group views and other children are left to the existing layout
        // rules.
        continue;
      }

      const tabs::TabInterface* tab = tab_view->GetTabInterface();
      if (!tab) {
        continue;
      }

      // Untagged tabs stay visible everywhere; it is far better for a stray
      // tab to show up than to silently vanish.
      bool visible = true;
      const auto it = tab_space_.find(tab->GetHandle());
      if (it != tab_space_.end() && !it->second.empty()) {
        visible = (it->second == current_space_id_);
      }

      tabs::TabInterface* live_tab = tab->GetHandle().Get();
      content::WebContents* live_contents =
          live_tab ? live_tab->GetContents() : nullptr;

      // A favorite's tab is represented by its tile in the favorites row, so
      // it never also gets a row in either tab-strip container.
      if (visible && live_contents && IsFavoriteTab(live_contents)) {
        visible = false;
      }

      // A link preview's tab is represented by the overlay card drawn over
      // the content area, so it gets no row either.  It is an untagged tab,
      // so the Space rule above left it visible.
      if (visible && live_contents && IsLinkPreviewTab(live_contents)) {
        visible = false;
      }

      // A materialized Avora pinned item's tab is represented by its row in
      // AvoraPinnedSectionView (see Rebuild()), so it must not also render
      // as a normal row in the daily/unpinned list -- same reasoning as the
      // Favorite rule above. Scoped to the unpinned container only: a
      // natively pinned tab keeps its own compact row in the pinned
      // container regardless of whether it also carries a kPinned marker
      // (from PinAndCreatePinnedItem() or BackfillNativePinnedTabs()) --
      // that marker only ever changes which row AvoraPinnedSectionView
      // draws for it, never whether the native strip shows it.
      if (visible && !is_pinned_container && live_contents &&
          IsPinnedItemTab(live_contents)) {
        visible = false;
      }

      // A pull request opened from a Live Folder is represented by its row
      // in AvoraLiveFoldersView, the same way a pinned item's tab is.
      if (visible && !is_pinned_container && live_contents &&
          IsLiveFolderTab(live_contents)) {
        visible = false;
      }

      if (tab_view->GetVisible() != visible) {
        tab_view->SetVisible(visible);
      }
    }
  }
  applying_ = false;
}

void AvoraSpaceTabFilter::ApplyVisibilityToSplit(SplitTabView& split_view) {
  bool visible = false;
  std::set<std::string> spaces;
  for (views::View* child : split_view.children()) {
    TabView* tab_view = views::AsViewClass<TabView>(child);
    const tabs::TabInterface* tab =
        tab_view ? tab_view->GetTabInterface() : nullptr;
    if (!tab) {
      continue;
    }
    // The row stands or falls as a whole, so a tab that would have no row of
    // its own still has its half of this one.
    tab_view->SetVisible(true);

    const auto it = tab_space_.find(tab->GetHandle());
    if (it == tab_space_.end() || it->second.empty()) {
      visible = true;
      continue;
    }
    visible |= it->second == current_space_id_;
    spaces.insert(it->second);
  }
  split_view.SetVisible(visible);

  // Posted: TabStripModel can't be changed from here, inside its own
  // notifications.
  if (spaces.size() > 1 && !separating_splits_) {
    separating_splits_ = true;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&AvoraSpaceTabFilter::SeparateSplitsAcrossSpaces,
                       weak_factory_.GetWeakPtr()));
  }
}

void AvoraSpaceTabFilter::SeparateSplitsAcrossSpaces() {
  separating_splits_ = false;
  TabStripModel* model = GetModel();
  if (!model) {
    return;
  }
  std::set<split_tabs::SplitTabId> to_separate;
  for (int i = 0; i < model->count(); ++i) {
    const std::optional<split_tabs::SplitTabId> split =
        model->GetTabAtIndex(i)->GetSplit();
    if (!split.has_value()) {
      continue;
    }
    std::set<std::string> spaces;
    for (tabs::TabInterface* tab : model->GetSplitData(*split)->ListTabs()) {
      const std::string space_id = GetTabSpaceId(tab->GetContents());
      if (!space_id.empty()) {
        spaces.insert(space_id);
      }
    }
    if (spaces.size() > 1) {
      to_separate.insert(*split);
    }
  }
  for (const split_tabs::SplitTabId& split : to_separate) {
    model->RemoveSplit(split);
  }
}

// ── Today tab expiry ────────────────────────────────────────────────────────

std::set<std::pair<std::string, std::string>>
AvoraSpaceTabFilter::CollectProtectedTodayTabs() const {
  std::set<std::pair<std::string, std::string>> protected_tabs;
  TabStripModel* model = GetModel();
  if (!model) {
    return protected_tabs;
  }

  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (!tab || !tab->GetContents()) {
      continue;
    }
    if (IsFavoriteTab(tab->GetContents())) {
      continue;
    }
    const GURL url = UrlForTab(tab);
    if (!IsUsableTabUrl(url)) {
      continue;
    }
    const auto it = tab_space_.find(tab->GetHandle());
    if (it == tab_space_.end() || it->second.empty()) {
      continue;
    }
    protected_tabs.emplace(it->second, url.spec());
  }
  return protected_tabs;
}

void AvoraSpaceTabFilter::SweepExpiredTodayTabsIfEnabled() {
  Profile* profile = browser_ ? browser_->GetProfile() : nullptr;
  if (!item_store_ || !profile) {
    return;
  }

  const base::TimeDelta max_age = GetTodayTabExpiry(profile->GetPrefs());
  if (!max_age.is_positive()) {
    return;
  }

  item_store_->SweepExpiredTodayItems(max_age, CollectProtectedTodayTabs());
}

void AvoraSpaceTabFilter::OnSessionRestored(Profile* profile,
                                            int /*num_tabs*/) {
  if (!browser_ || profile != browser_->GetProfile()) {
    return;
  }
  session_restore_subscription_ = {};
  ResolvePendingTabAssignments(/*sync_store=*/true);
  ApplyVisibility();
  SweepExpiredTodayTabsIfEnabled();

  // Daily tab sync sits out the restore, while each window's tabs are still
  // arriving and would look missing to the others.  Now every window has its
  // own back, and the first flush merges them.
  ScheduleDailyTabSync();
}

// ── Cross-window daily tab sync ─────────────────────────────────────────────

bool AvoraSpaceTabFilter::CarryOverDailyTabs() {
  TabStripModel* model = GetModel();
  if (!model) {
    return false;
  }
  pending_initial_join_ = false;

  tabs::TabInterface* selection = JoinSpace(current_space_id_);
  if (model->empty()) {
    return false;
  }

  // The other windows' selection was a Favorite, a pinned item, or a tab in
  // another Space, none of which came along; land on the top of the list.
  for (int i = 0; i < model->count() && !selection; ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    const auto it = tab ? tab_space_.find(tab->GetHandle()) : tab_space_.end();
    if (it != tab_space_.end() && it->second == current_space_id_) {
      selection = tab;
    }
  }
  if (selection) {
    model->ActivateTab(selection);
  }
  RememberActiveTab();
  return true;
}

std::vector<AvoraSpaceTabFilter*> AvoraSpaceTabFilter::SyncPeers() const {
  std::vector<AvoraSpaceTabFilter*> peers;
  if (!ParticipatesInDailyTabSync()) {
    return peers;
  }
  for (AvoraSpaceTabFilter* filter : DailyTabSyncRegistry()) {
    if (filter != this && filter->ParticipatesInDailyTabSync() &&
        filter->browser_->GetProfile() == browser_->GetProfile()) {
      peers.push_back(filter);
    }
  }
  return peers;
}

bool AvoraSpaceTabFilter::ParticipatesInDailyTabSync() const {
  return browser_ && browser_->GetProfile() && GetModel() &&
         browser_->GetType() == BrowserWindowInterface::TYPE_NORMAL;
}

bool AvoraSpaceTabFilter::IsDailyTab(tabs::TabInterface* tab) const {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!contents || tab->IsPinned()) {
    return false;
  }
  const auto it = tab_space_.find(tab->GetHandle());
  if (it == tab_space_.end() || it->second.empty()) {
    return false;
  }
  return !IsFavoriteTab(contents) && !IsPinnedItemTab(contents) &&
         !IsLinkPreviewTab(contents) && !IsLiveFolderTab(contents);
}

bool AvoraSpaceTabFilter::CanCopyDailyTab(tabs::TabInterface* tab) const {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!contents || GetTabGuid(contents).empty()) {
    return false;
  }
  // A tab that has not committed anything yet holds only the initial blank
  // entry, which cannot be copied.  It is announced on its first commit, a
  // moment later for a New Tab.
  content::NavigationEntry* entry =
      contents->GetController().GetLastCommittedEntry();
  return entry && !entry->IsInitialEntry();
}

tabs::TabInterface* AvoraSpaceTabFilter::FindTabByGuid(
    const std::string& guid) const {
  TabStripModel* model = GetModel();
  if (!model || guid.empty()) {
    return nullptr;
  }
  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    content::WebContents* contents = tab ? tab->GetContents() : nullptr;
    if (!contents) {
      continue;
    }
    // A restored tab can carry its GUID on its entries before the filter has
    // claimed it.  It is still the same tab, and must not be copied in again.
    std::string tab_guid = GetTabGuid(contents);
    if (tab_guid.empty()) {
      tab_guid = FindTabGuidInController(&contents->GetController());
    }
    if (tab_guid == guid) {
      return tab;
    }
  }
  return nullptr;
}

bool AvoraSpaceTabFilter::HasJoinedSpace(const std::string& space_id) const {
  if (joined_spaces_.contains(space_id)) {
    return true;
  }
  for (const auto& [handle, tab_space] : tab_space_) {
    if (tab_space == space_id) {
      return true;
    }
  }
  return false;
}

void AvoraSpaceTabFilter::ScheduleDailyTabSync() {
  if (sync_flush_scheduled_ || !ParticipatesInDailyTabSync()) {
    return;
  }
  sync_flush_scheduled_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&AvoraSpaceTabFilter::FlushDailyTabSync,
                                weak_factory_.GetWeakPtr()));
}

void AvoraSpaceTabFilter::FlushDailyTabSync() {
  sync_flush_scheduled_ = false;
  TabStripModel* model = GetModel();
  Profile* profile = browser_ ? browser_->GetProfile() : nullptr;
  if (!model || !profile || !ParticipatesInDailyTabSync() ||
      SessionRestore::IsRestoring(profile)) {
    return;
  }

  if (pending_initial_join_) {
    pending_initial_join_ = false;
    JoinSpace(current_space_id_);
  }

  std::erase_if(published_tabs_, [](const auto& entry) {
    return entry.first.Get() == nullptr;
  });
  std::erase_if(DailyTabsInTransit(), [](const tabs::TabHandle& handle) {
    return handle.Get() == nullptr;
  });

  // A peer acts on its own strip only, and never closes its last tab, so none
  // goes away mid-flush.  Re-checked anyway: a stale pointer here would be a
  // use-after-free in a different window.
  const auto still_registered = [](AvoraSpaceTabFilter* peer) {
    return std::ranges::contains(DailyTabSyncRegistry(), peer);
  };
  const std::vector<AvoraSpaceTabFilter*> peers = SyncPeers();

  // Announcements go out before closes; see the commit wait below.

  // ── New daily tabs, and ones moved to another Space ──
  std::map<std::string, tabs::TabHandle> holders;
  for (const auto& [handle, published] : published_tabs_) {
    holders[published.guid] = handle;
  }
  std::vector<tabs::TabHandle> announce;
  std::vector<tabs::TabHandle> superseded;
  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (!IsDailyTab(tab) || !CanCopyDailyTab(tab)) {
      continue;
    }
    const tabs::TabHandle handle = tab->GetHandle();
    std::string guid = GetTabGuid(tab->GetContents());
    const std::string space_id = tab_space_[handle];
    const auto published = published_tabs_.find(handle);
    if (published != published_tabs_.end() &&
        published->second.guid == guid &&
        published->second.space_id == space_id) {
      continue;
    }

    // Two tabs in this window speaking for one logical tab.  A tab dragged in
    // from another window takes over from the copy this window already had of
    // it; anything else -- Duplicate Tab copies the GUID along with the
    // history -- is a new tab, and gets a GUID of its own.
    const bool moved_here = DailyTabsInTransit().erase(handle) > 0;
    const auto holder = holders.find(guid);
    if (holder != holders.end() && holder->second != handle &&
        holder->second.Get()) {
      if (moved_here) {
        published_tabs_.erase(holder->second);
        superseded.push_back(holder->second);
      } else {
        guid = base::Uuid::GenerateRandomV4().AsLowercaseString();
        SetTabGuid(tab->GetContents(), guid);
      }
    }
    published_tabs_[handle] = {guid, space_id};
    holders[guid] = handle;
    announce.push_back(handle);
  }

  for (const tabs::TabHandle& handle : superseded) {
    if (tabs::TabInterface* tab = handle.Get()) {
      CloseDailyTabCopy(tab);
    }
  }

  for (const tabs::TabHandle& handle : announce) {
    tabs::TabInterface* tab = handle.Get();
    const int index = tab ? model->GetIndexOfTab(tab) : TabStripModel::kNoTab;
    if (index == TabStripModel::kNoTab) {
      continue;
    }
    const PublishedTab published = published_tabs_[handle];

    // Its neighbours in this Space, so the other windows can put their copy
    // in the same place in the list.
    const auto neighbour_guid = [&](int from, int step) {
      for (int i = from; i >= 0 && i < model->count(); i += step) {
        const auto it =
            published_tabs_.find(model->GetTabAtIndex(i)->GetHandle());
        if (it != published_tabs_.end() &&
            it->second.space_id == published.space_id) {
          return it->second.guid;
        }
      }
      return std::string();
    };
    const std::string predecessor = neighbour_guid(index - 1, -1);
    const std::string successor = neighbour_guid(index + 1, 1);

    for (AvoraSpaceTabFilter* peer : peers) {
      if (still_registered(peer)) {
        peer->ApplyRemoteUpsert(tab, published.guid, published.space_id,
                                predecessor, successor);
      }
    }
    if (SharedTabsDelegate* delegate = SharedTabsDelegateInstance()) {
      delegate->OnSharedTabOpened(
          profile, SharedTabFor(tab, published.guid, published.space_id),
          predecessor, successor);
    }
  }

  // ── Navigations ──
  std::set<tabs::TabHandle> navigated = std::move(navigated_tabs_);
  navigated_tabs_.clear();
  for (const tabs::TabHandle& handle : navigated) {
    tabs::TabInterface* tab = handle.Get();
    const auto published = published_tabs_.find(handle);
    if (!tab || !tab->GetContents() || published == published_tabs_.end()) {
      continue;
    }
    // Only a page that is actually loaded leads.  A copy that has never been
    // opened changes only because it was made to follow, and would otherwise
    // echo that straight back.
    if (tab->GetContents()->GetController().NeedsReload()) {
      continue;
    }
    for (AvoraSpaceTabFilter* peer : peers) {
      if (still_registered(peer)) {
        peer->ApplyRemoteNavigation(tab, published->second.guid);
      }
    }
    // The user took the tab somewhere: that beats a page another device
    // moved it to while it was on screen.
    deferred_shared_navigations_.erase(published->second.guid);
    if (SharedTabsDelegate* delegate = SharedTabsDelegateInstance()) {
      delegate->OnSharedTabNavigated(
          profile, SharedTabFor(tab, published->second.guid,
                                published->second.space_id));
    }
  }

  ApplyDeferredSharedNavigations();
  EnforceWarmBudget();

  // ── Closed here, or no longer in the daily list ──
  for (auto it = published_tabs_.begin(); it != published_tabs_.end();) {
    tabs::TabInterface* tab = it->first.Get();
    if (!tab) {
      it = published_tabs_.erase(it);
    } else if (!IsDailyTab(tab)) {
      // Pinned, made a Favorite, or filed into a pinned folder: the tab is
      // still open here, but the other windows show it through the pinned
      // section or Favorites row now, so their daily copies have to go.
      pending_removed_guids_.insert(it->second.guid);
      it = published_tabs_.erase(it);
    } else {
      ++it;
    }
  }

  // Closing a Space's last daily tab makes this window open a blank one, which
  // cannot be announced until it commits.  The close waits for it: a window
  // that loses its last tab of the Space before receiving this one opens a
  // blank tab of its own, and each window then mirrors the other's.  The
  // commit raises a tab change, which schedules the next flush.
  const bool awaiting_first_commit = [&] {
    for (int i = 0; i < model->count(); ++i) {
      content::WebContents* contents = model->GetWebContentsAt(i);
      content::NavigationEntry* entry =
          contents ? contents->GetController().GetLastCommittedEntry()
                   : nullptr;
      if (contents && (!entry || entry->IsInitialEntry()) &&
          contents->GetController().GetPendingEntry()) {
        return true;
      }
    }
    return false;
  }();
  if (awaiting_first_commit) {
    return;
  }

  std::set<std::string> removed = std::move(pending_removed_guids_);
  pending_removed_guids_.clear();
  for (const std::string& guid : removed) {
    // Closed and reopened (Cmd+Shift+T) before the flush: still open.
    const bool still_open = std::ranges::any_of(
        published_tabs_,
        [&](const auto& entry) { return entry.second.guid == guid; });
    if (still_open) {
      continue;
    }
    for (AvoraSpaceTabFilter* peer : peers) {
      if (still_registered(peer)) {
        peer->ApplyRemoteRemove(guid);
      }
    }
    deferred_shared_navigations_.erase(guid);
    if (SharedTabsDelegate* delegate = SharedTabsDelegateInstance()) {
      delegate->OnSharedTabClosed(profile, guid);
    }
  }
}

tabs::TabInterface* AvoraSpaceTabFilter::JoinSpace(
    const std::string& space_id) {
  TabStripModel* model = GetModel();
  Profile* profile = browser_ ? browser_->GetProfile() : nullptr;
  if (!model || !profile || space_id.empty() ||
      !ParticipatesInDailyTabSync() || SessionRestore::IsRestoring(profile)) {
    return nullptr;
  }
  joined_spaces_.insert(space_id);

  // Most recently used window first: its order is the one the user last saw,
  // and its selection is the one to land on.
  std::vector<AvoraSpaceTabFilter*> peers = SyncPeers();
  std::map<BrowserWindowInterface*, int> recency;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* browser) {
        recency.emplace(browser, static_cast<int>(recency.size()));
        return true;
      });
  const auto rank = [&](const AvoraSpaceTabFilter* peer) {
    const auto it = recency.find(peer->browser_);
    return it != recency.end() ? it->second : std::numeric_limits<int>::max();
  };
  std::ranges::stable_sort(peers, {}, rank);

  tabs::TabInterface* selection = nullptr;
  bool added = false;

  // The first tab put in an empty strip becomes its active tab, and the
  // active tab loads.  So into an empty window the tab that is to be selected
  // goes first, and the rest are fitted around it, or a background copy
  // would load for nothing.
  const bool was_empty = model->empty();
  if (was_empty) {
    for (AvoraSpaceTabFilter* peer : peers) {
      tabs::TabInterface* peer_active = peer->GetModel()->GetActiveTab();
      if (peer->IsDailyTab(peer_active) && peer->CanCopyDailyTab(peer_active) &&
          peer->tab_space_.at(peer_active->GetHandle()) == space_id) {
        selection = InsertDailyTabCopy(peer_active->GetContents(), space_id,
                                       GetTabGuid(peer_active->GetContents()),
                                       /*index=*/0, /*activate=*/true);
        added = selection != nullptr;
        break;
      }
    }
  }

  for (AvoraSpaceTabFilter* peer : peers) {
    TabStripModel* peer_model = peer->GetModel();
    content::WebContents* const peer_active =
        peer_model->GetActiveWebContents();

    // Where the next missing tab goes: right after the last tab of the Space
    // matched so far, so a tab missing from the middle of the list lands in
    // the middle here too.  In a window that was empty, the tabs ahead of the
    // selection go in front of it.
    int insert_at =
        was_empty && peer == peers.front() ? 0 : TabStripModel::kNoTab;
    for (int i = 0; i < peer_model->count(); ++i) {
      tabs::TabInterface* peer_tab = peer_model->GetTabAtIndex(i);
      if (!peer->IsDailyTab(peer_tab) || !peer->CanCopyDailyTab(peer_tab) ||
          peer->tab_space_.at(peer_tab->GetHandle()) != space_id) {
        continue;
      }
      const std::string guid = GetTabGuid(peer_tab->GetContents());
      tabs::TabInterface* local = FindTabByGuid(guid);
      if (!local) {
        local = InsertDailyTabCopy(
            peer_tab->GetContents(), space_id, guid,
            insert_at != TabStripModel::kNoTab ? insert_at : model->count());
        if (!local) {
          continue;
        }
        added = true;
      }
      insert_at = model->GetIndexOfTab(local) + 1;
      if (!selection && peer_tab->GetContents() == peer_active) {
        selection = local;
      }
    }
  }

  // The account's tabs in this Space that no window here has: opened on
  // another device, or left behind when the last window showing them closed.
  if (SharedTabsDelegate* delegate = SharedTabsDelegateInstance()) {
    std::vector<std::string> before;
    for (const SharedTab& shared :
         delegate->SharedTabsForSpace(profile, space_id)) {
      if (!FindTabByGuid(shared.guid)) {
        tabs::TabInterface* local =
            InsertSharedTab(shared, SharedInsertIndex(space_id, before));
        added = added || local != nullptr;
        if (!selection && was_empty) {
          selection = local;
        }
      }
      before.push_back(shared.guid);
    }
  }

  if (added) {
    ApplyVisibility();
    SyncTabsToStore();
  }
  return selection;
}

std::unique_ptr<content::WebContents> AvoraSpaceTabFilter::CreateDailyTabCopy(
    content::WebContents* source,
    const std::string& space_id,
    const std::string& guid) {
  Profile* profile = browser_ ? browser_->GetProfile() : nullptr;
  if (!profile || !source) {
    return nullptr;
  }

  // The source's SiteInstance, as WebContents::Clone() does: the copied
  // entries refer to it, and it is already in the Space's storage partition,
  // so the copy signs in as the same identity.
  content::WebContents::CreateParams create_params(profile,
                                                   source->GetSiteInstance());
  create_params.initially_hidden = true;
  std::unique_ptr<content::WebContents> copy =
      content::WebContents::Create(create_params);
  if (!copy) {
    return nullptr;
  }

  // Back/forward history, titles, and sessionStorage come along.  With
  // |needs_reload| nothing is fetched until the copy is first shown, so a copy
  // costs no renderer and no network until the user opens it.
  copy->GetController().CopyStateFrom(&source->GetController(),
                                      /*needs_reload=*/true);
  SetTabGuid(copy.get(), guid);
  SetTabSpaceId(copy.get(), space_id);
  return copy;
}

tabs::TabInterface* AvoraSpaceTabFilter::InsertDailyTabCopy(
    content::WebContents* source,
    const std::string& space_id,
    const std::string& guid,
    int index,
    bool activate) {
  TabStripModel* model = GetModel();
  std::unique_ptr<content::WebContents> copy =
      CreateDailyTabCopy(source, space_id, guid);
  if (!model || !copy) {
    return nullptr;
  }

  // Tagged here rather than by AdoptUntaggedTabs(), which would claim the tab
  // for whichever Space this window happens to be showing.
  int inserted;
  {
    base::AutoReset<bool> applying(&applying_, true);
    inserted = model->InsertWebContentsAt(
        InsertionIndexOutsideSplit(*model, index), std::move(copy),
        activate ? AddTabTypes::ADD_ACTIVE : AddTabTypes::ADD_NONE);
  }
  tabs::TabInterface* tab = model->GetTabAtIndex(inserted);
  if (!tab) {
    return nullptr;
  }
  tab_space_[tab->GetHandle()] = space_id;
  published_tabs_[tab->GetHandle()] = {guid, space_id};
  EnsureDiscardObserver(tab);
  return tab;
}

void AvoraSpaceTabFilter::CloseDailyTabCopy(tabs::TabInterface* tab) {
  TabStripModel* model = GetModel();
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!model || !contents) {
    return;
  }
  published_tabs_.erase(tab->GetHandle());
  navigated_tabs_.erase(tab->GetHandle());

  // The last tab in a window takes the window with it, and a tab closed in
  // some other window is no reason to lose this one.
  if (model->count() == 1 && model->delegate()) {
    model->delegate()->AddTabAt(GURL(), /*index=*/-1, /*foreground=*/true);
  }

  const std::string guid = GetTabGuid(contents);
  remote_closing_guids_.insert(guid);
  model->CloseWebContents(contents, TabCloseTypes::CLOSE_NONE);
  remote_closing_guids_.erase(guid);
}

void AvoraSpaceTabFilter::ApplyRemoteUpsert(
    tabs::TabInterface* source,
    const std::string& guid,
    const std::string& space_id,
    const std::string& predecessor_guid,
    const std::string& successor_guid) {
  TabStripModel* model = GetModel();
  if (!model || !source || !source->GetContents()) {
    return;
  }

  if (tabs::TabInterface* local = FindTabByGuid(guid)) {
    // Already here.  All an announcement can change is the Space: "Move to
    // Space" in another window moves this copy too.  AdoptRetaggedTab() is
    // the same path a move made in this window takes.
    if (!IsDailyTab(local) ||
        tab_space_.at(local->GetHandle()) == space_id) {
      return;
    }
    SetTabSpaceId(local->GetContents(), space_id);
    if (AdoptRetaggedTab(local)) {
      const auto published = published_tabs_.find(local->GetHandle());
      if (published != published_tabs_.end()) {
        published->second.space_id = space_id;
      }
      SyncTabsToStore();
      ApplyVisibility();
    }
    return;
  }

  // A Space this window has never shown gets its tabs when it is first
  // entered (OnWindowActiveSpaceChanged), not one at a time in the meantime.
  if (!HasJoinedSpace(space_id)) {
    return;
  }

  int index = model->count();
  if (tabs::TabInterface* before = FindTabByGuid(predecessor_guid)) {
    index = model->GetIndexOfTab(before) + 1;
  } else if (tabs::TabInterface* after = FindTabByGuid(successor_guid)) {
    index = model->GetIndexOfTab(after);
  }
  if (InsertDailyTabCopy(source->GetContents(), space_id, guid, index)) {
    ApplyVisibility();
    SyncTabsToStore();
  }
}

void AvoraSpaceTabFilter::ApplyRemoteRemove(const std::string& guid) {
  tabs::TabInterface* tab = FindTabByGuid(guid);
  if (tab && IsDailyTab(tab)) {
    CloseDailyTabCopy(tab);
  }
}

void AvoraSpaceTabFilter::ApplyRemoteNavigation(tabs::TabInterface* source,
                                                const std::string& guid) {
  TabStripModel* model = GetModel();
  tabs::TabInterface* local = FindTabByGuid(guid);
  content::WebContents* source_contents =
      source ? source->GetContents() : nullptr;
  if (!model || !local || !source_contents || !IsDailyTab(local)) {
    return;
  }
  content::WebContents* local_contents = local->GetContents();

  // The page the user has in front of them in this window is theirs; it is
  // never pulled out from under them.
  if (model->GetActiveWebContents() == local_contents) {
    return;
  }

  if (UrlForTab(local) == UrlForTab(source)) {
    // Same page.  All there is to carry is a title that arrived after the
    // commit, and only to a copy that has never loaded and so has no title of
    // its own.
    if (!local_contents->GetController().NeedsReload()) {
      return;
    }
    content::NavigationEntry* entry =
        local_contents->GetController().GetLastCommittedEntry();
    const std::u16string& title = source_contents->GetTitle();
    if (!entry || entry->GetTitle() == title) {
      return;
    }
    entry->SetTitle(title);
    local_contents->NotifyNavigationStateChanged(content::INVALIDATE_TYPE_TITLE);
    return;
  }

  // Another page: replace this copy with a fresh, unloaded one of the source.
  // Whatever this copy had loaded in the background is dropped, which is the
  // price of the list showing the same page in every window.
  std::unique_ptr<content::WebContents> copy = CreateDailyTabCopy(
      source_contents, tab_space_.at(local->GetHandle()), guid);
  if (!copy) {
    return;
  }
  std::unique_ptr<content::WebContents> retired;
  {
    base::AutoReset<bool> applying(&applying_, true);
    retired = model->DiscardWebContents(local_contents, std::move(copy));
  }
  ApplyVisibility();
}

// ── Shared tabs ─────────────────────────────────────────────────────────────

// static
void AvoraSpaceTabFilter::SetSharedTabsDelegate(SharedTabsDelegate* delegate) {
  SharedTabsDelegateInstance() = delegate;
}

// static
std::vector<AvoraSpaceTabFilter::SharedTab>
AvoraSpaceTabFilter::CollectSharedTabs(Profile* profile) {
  std::vector<SharedTab> tabs;
  std::set<std::string> seen;
  for (AvoraSpaceTabFilter* filter : DailyTabSyncRegistry()) {
    TabStripModel* model = filter->GetModel();
    if (!model || !filter->ParticipatesInDailyTabSync() ||
        filter->browser_->GetProfile() != profile) {
      continue;
    }
    for (int i = 0; i < model->count(); ++i) {
      tabs::TabInterface* tab = model->GetTabAtIndex(i);
      if (!filter->IsDailyTab(tab) || !filter->CanCopyDailyTab(tab)) {
        continue;
      }
      const std::string guid = GetTabGuid(tab->GetContents());
      if (guid.empty() || !seen.insert(guid).second) {
        continue;
      }
      tabs.push_back(filter->SharedTabFor(
          tab, guid, filter->tab_space_.at(tab->GetHandle())));
    }
  }
  return tabs;
}

// static
void AvoraSpaceTabFilter::UpsertSharedTab(
    Profile* profile,
    const SharedTab& tab,
    const std::vector<std::string>& before) {
  // A copy: a filter's work can close a window and so remove its filter.
  const std::vector<AvoraSpaceTabFilter*> filters = DailyTabSyncRegistry();
  for (AvoraSpaceTabFilter* filter : filters) {
    if (std::ranges::contains(DailyTabSyncRegistry(), filter) &&
        filter->ParticipatesInDailyTabSync() &&
        filter->browser_->GetProfile() == profile) {
      filter->ApplySharedUpsert(tab, before);
    }
  }
}

// static
void AvoraSpaceTabFilter::RemoveSharedTab(Profile* profile,
                                          const std::string& guid) {
  const std::vector<AvoraSpaceTabFilter*> filters = DailyTabSyncRegistry();
  for (AvoraSpaceTabFilter* filter : filters) {
    if (std::ranges::contains(DailyTabSyncRegistry(), filter) &&
        filter->ParticipatesInDailyTabSync() &&
        filter->browser_->GetProfile() == profile) {
      filter->deferred_shared_navigations_.erase(guid);
      filter->ApplyRemoteRemove(guid);
    }
  }
}

AvoraSpaceTabFilter::SharedTab AvoraSpaceTabFilter::SharedTabFor(
    tabs::TabInterface* tab,
    const std::string& guid,
    const std::string& space_id) const {
  SharedTab shared;
  shared.guid = guid;
  shared.space_id = space_id;
  shared.url = UrlForTab(tab);
  if (tab->GetContents()) {
    shared.title = tab->GetContents()->GetTitle();
  }
  return shared;
}

std::unique_ptr<content::WebContents>
AvoraSpaceTabFilter::CreateSharedTabContents(const SharedTab& tab) {
  Profile* profile = browser_ ? browser_->GetProfile() : nullptr;
  if (!profile || !tab.url.is_valid()) {
    return nullptr;
  }
  // In the Space's storage partition, so the page signs in as the Space's
  // identity, and hidden with one restored entry, so nothing loads until the
  // user selects it.
  content::WebContents::CreateParams create_params(
      profile, GetSiteInstanceForSpace(profile, tab.space_id, tab.url));
  create_params.initially_hidden = true;
  std::unique_ptr<content::WebContents> contents =
      content::WebContents::Create(create_params);
  if (!contents) {
    return nullptr;
  }
  std::unique_ptr<content::NavigationEntry> entry =
      content::NavigationController::CreateNavigationEntry(
          tab.url, content::Referrer(), /*initiator_origin=*/std::nullopt,
          /*initiator_base_url=*/std::nullopt,
          ui::PAGE_TRANSITION_AUTO_TOPLEVEL, /*is_renderer_initiated=*/false,
          /*extra_headers=*/std::string(), profile,
          /*blob_url_loader_factory=*/nullptr);
  entry->SetTitle(tab.title);
  std::vector<std::unique_ptr<content::NavigationEntry>> entries;
  entries.push_back(std::move(entry));
  contents->GetController().Restore(0, content::RestoreType::kRestored,
                                    &entries);
  SetTabGuid(contents.get(), tab.guid);
  SetTabSpaceId(contents.get(), tab.space_id);
  return contents;
}

int AvoraSpaceTabFilter::SharedInsertIndex(
    const std::string& space_id,
    const std::vector<std::string>& before) const {
  TabStripModel* model = GetModel();
  // After the last tab ahead of it that this window has.
  for (auto it = before.rbegin(); it != before.rend(); ++it) {
    if (tabs::TabInterface* tab = FindTabByGuid(*it)) {
      return model->GetIndexOfTab(tab) + 1;
    }
  }
  // Nothing ahead of it here: first in the Space.
  for (int i = 0; i < model->count(); ++i) {
    const auto it = tab_space_.find(model->GetTabAtIndex(i)->GetHandle());
    if (it != tab_space_.end() && it->second == space_id &&
        IsDailyTab(model->GetTabAtIndex(i))) {
      return i;
    }
  }
  return model->count();
}

tabs::TabInterface* AvoraSpaceTabFilter::InsertSharedTab(const SharedTab& tab,
                                                         int index) {
  TabStripModel* model = GetModel();
  std::unique_ptr<content::WebContents> contents = CreateSharedTabContents(tab);
  if (!model || !contents) {
    return nullptr;
  }
  int inserted;
  {
    base::AutoReset<bool> applying(&applying_, true);
    inserted = model->InsertWebContentsAt(
        InsertionIndexOutsideSplit(*model,
                                   std::clamp(index, 0, model->count())),
        std::move(contents),
        model->empty() ? AddTabTypes::ADD_ACTIVE : AddTabTypes::ADD_NONE);
  }
  tabs::TabInterface* local = model->GetTabAtIndex(inserted);
  if (!local) {
    return nullptr;
  }
  // Published, so the next flush doesn't announce it back: the other windows
  // insert their own copies, and the account already has it.
  tab_space_[local->GetHandle()] = tab.space_id;
  published_tabs_[local->GetHandle()] = {tab.guid, tab.space_id};
  EnsureDiscardObserver(local);
  return local;
}

void AvoraSpaceTabFilter::ApplySharedUpsert(
    const SharedTab& tab,
    const std::vector<std::string>& before) {
  TabStripModel* model = GetModel();
  if (!model || tab.guid.empty() || tab.space_id.empty()) {
    return;
  }
  tabs::TabInterface* local = FindTabByGuid(tab.guid);
  if (!local) {
    if (HasJoinedSpace(tab.space_id) &&
        InsertSharedTab(tab, SharedInsertIndex(tab.space_id, before))) {
      ApplyVisibility();
      SyncTabsToStore();
    }
    return;
  }
  if (!IsDailyTab(local)) {
    return;
  }

  // Moved to another Space.
  const tabs::TabHandle handle = local->GetHandle();
  if (tab_space_[handle] != tab.space_id) {
    SetTabSpaceId(local->GetContents(), tab.space_id);
    tab_space_[handle] = tab.space_id;
    if (auto published = published_tabs_.find(handle);
        published != published_tabs_.end()) {
      published->second.space_id = tab.space_id;
    }
    ApplyVisibility();
    SyncTabsToStore();
  }

  NavigateSharedCopy(local, tab);
}

void AvoraSpaceTabFilter::NavigateSharedCopy(tabs::TabInterface* local,
                                             const SharedTab& tab) {
  TabStripModel* model = GetModel();
  content::WebContents* contents = local->GetContents();
  if (!model || !contents || !tab.url.is_valid()) {
    return;
  }
  if (UrlForTab(local) == tab.url) {
    // Already there: no reload. A copy that has never loaded takes the title.
    if (contents->GetController().NeedsReload() && !tab.title.empty()) {
      content::NavigationEntry* entry =
          contents->GetController().GetLastCommittedEntry();
      if (entry && entry->GetTitle() != tab.title) {
        entry->SetTitle(tab.title);
        contents->NotifyNavigationStateChanged(content::INVALIDATE_TYPE_TITLE);
      }
    }
    deferred_shared_navigations_.erase(tab.guid);
    return;
  }

  // The page the user has in front of them is never pulled out from under
  // them; it follows once they leave it.
  if (model->GetActiveWebContents() == contents) {
    deferred_shared_navigations_[tab.guid] = tab;
    return;
  }
  deferred_shared_navigations_.erase(tab.guid);

  // Another page: a fresh, unloaded copy of it replaces this one.
  SharedTab replacement = tab;
  replacement.space_id = tab_space_.at(local->GetHandle());
  std::unique_ptr<content::WebContents> fresh =
      CreateSharedTabContents(replacement);
  if (!fresh) {
    return;
  }
  std::unique_ptr<content::WebContents> retired;
  {
    base::AutoReset<bool> applying(&applying_, true);
    retired = model->DiscardWebContents(contents, std::move(fresh));
  }
  ApplyVisibility();
}

void AvoraSpaceTabFilter::EnforceWarmBudget() {
  TabStripModel* model = GetModel();
  if (!model || applying_) {
    return;
  }
  content::WebContents* const active = model->GetActiveWebContents();
  // The other half of the active tab's pair is on screen too.
  const tabs::TabInterface* const active_tab = model->GetActiveTab();
  const std::optional<split_tabs::SplitTabId> active_split =
      active_tab ? active_tab->GetSplit() : std::nullopt;
  std::vector<tabs::TabInterface*> warm;
  size_t pinned_warm = 0;
  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    content::WebContents* contents = tab->GetContents();
    if (!contents || contents == active || !IsDailyTab(tab) ||
        (active_split.has_value() && tab->GetSplit() == active_split) ||
        contents->GetController().NeedsReload() || contents->WasDiscarded()) {
      continue;
    }
    if (contents->IsCurrentlyAudible() || contents->IsBeingCaptured() ||
        contents->NeedToFireBeforeUnloadOrUnloadEvents()) {
      // Counted, never unloaded.
      ++pinned_warm;
      continue;
    }
    warm.push_back(tab);
  }
  if (warm.size() + pinned_warm <= kWarmTabBudget) {
    return;
  }
  std::ranges::sort(warm, {}, [](tabs::TabInterface* tab) {
    return tab->GetContents()->GetLastActiveTimeTicks();
  });
  size_t excess = warm.size() + pinned_warm - kWarmTabBudget;
  for (tabs::TabInterface* tab : warm) {
    if (excess == 0) {
      break;
    }
    const auto space = tab_space_.find(tab->GetHandle());
    const std::string guid = GetTabGuid(tab->GetContents());
    if (space == tab_space_.end() || guid.empty()) {
      continue;
    }
    std::unique_ptr<content::WebContents> cold =
        CreateDailyTabCopy(tab->GetContents(), space->second, guid);
    if (!cold) {
      continue;
    }
    std::unique_ptr<content::WebContents> retired;
    {
      base::AutoReset<bool> applying(&applying_, true);
      retired = model->DiscardWebContents(tab->GetContents(), std::move(cold));
    }
    --excess;
  }
}

void AvoraSpaceTabFilter::ApplyDeferredSharedNavigations() {
  TabStripModel* model = GetModel();
  if (!model || deferred_shared_navigations_.empty()) {
    return;
  }
  std::vector<SharedTab> ready;
  for (const auto& [guid, tab] : deferred_shared_navigations_) {
    tabs::TabInterface* local = FindTabByGuid(guid);
    if (!local) {
      continue;
    }
    if (model->GetActiveWebContents() != local->GetContents()) {
      ready.push_back(tab);
    }
  }
  std::erase_if(deferred_shared_navigations_, [&](const auto& entry) {
    return !FindTabByGuid(entry.first);
  });
  for (const SharedTab& tab : ready) {
    if (tabs::TabInterface* local = FindTabByGuid(tab.guid)) {
      NavigateSharedCopy(local, tab);
    }
  }
}

bool CarryDailyTabsIntoNewWindow(BrowserWindowInterface* browser) {
  for (AvoraSpaceTabFilter* filter : DailyTabSyncRegistry()) {
    if (filter->browser_ == browser) {
      return filter->CarryOverDailyTabs();
    }
  }
  return false;
}

}  // namespace avora
