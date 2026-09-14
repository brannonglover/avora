// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/tabs/common/avora_space_tab_filter.h"

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/timer/timer.h"
#include "chrome/browser/avora/avora_prefs.h"
#include "chrome/browser/avora/avora_tab_guid.h"
#include "chrome/browser/avora/avora_tab_space.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/views/avora/avora_favorite_tab_marker.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/common/pinned_tab_container_view.h"
#include "chrome/browser/ui/views/tabs/common/unpinned_tab_container_view.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace avora {

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
  }

  if (TabStripModel* model = GetModel()) {
    model->AddObserver(this);
  }

  // Order matters: recover last session's GUID assignments before URL fallback,
  // and both before AdoptUntaggedTabs().
  RestoreTagsFromGuid();
  RestoreTagsFromStore();
  AdoptFavoriteTabs();
  AdoptUntaggedTabs();
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
}

AvoraSpaceTabFilter::~AvoraSpaceTabFilter() {
  if (window_space_state_) {
    window_space_state_->RemoveObserver(this);
  }
  if (space_manager_) {
    space_manager_->RemoveObserver(this);
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
  if (applying_) {
    return;
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

  RestoreActiveTabFor(space_id);

  AdoptUntaggedTabs();
  ApplyVisibility();
  SyncTabsToStore();
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

  item_store_->ReplaceItemsOfType(SidebarItemType::kTabState, items,
                                  /*notify=*/false);
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
    if (ShouldDeferSpaceAssignment(tab)) {
      continue;
    }
    tab_space_[handle] = current_space_id_;
    SetTabSpaceId(tab->GetContents(), current_space_id_);
    GetOrCreateTabGuid(tab->GetContents());
  }
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

// ── Visibility ──────────────────────────────────────────────────────────────

void AvoraSpaceTabFilter::ApplyVisibility() {
  if (!tab_strip_view_ || current_space_id_.empty()) {
    return;
  }

  // Both containers are filtered.  Pinning is a per-Space decision just like
  // any other tab: a tab pinned in one Space must not surface in the others.
  views::View* const containers[] = {
      tab_strip_view_->GetUnpinnedTabsContainer(),
      tab_strip_view_->GetPinnedTabsContainer(),
  };

  applying_ = true;
  for (views::View* container : containers) {
    if (!container) {
      continue;
    }
    for (views::View* child : container->children()) {
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

      // A favorite's tab is represented by its tile in the favorites row, so
      // it never also gets a row in the daily list below.
      tabs::TabInterface* live_tab = tab->GetHandle().Get();
      if (visible && live_tab && IsFavoriteTab(live_tab->GetContents())) {
        visible = false;
      }

      if (tab_view->GetVisible() != visible) {
        tab_view->SetVisible(visible);
      }
    }
  }
  applying_ = false;
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
}

}  // namespace avora
