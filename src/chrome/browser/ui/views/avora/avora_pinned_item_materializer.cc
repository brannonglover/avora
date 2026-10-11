// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_pinned_item_materializer.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/avora/avora_pinned_folders.h"
#include "chrome/browser/avora/avora_pinned_items.h"
#include "chrome/browser/avora/avora_tab_space.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_tab_marker.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"
#include "url/url_constants.h"

namespace avora {

namespace {

// |url| stripped of the parts that can differ without the user having gone
// anywhere.  Applied to both sides of every comparison, so http targets keep
// matching http pages -- the upgrade only ever removes a difference.
GURL CanonicalizeForTargetComparison(const GURL& url) {
  GURL::Replacements replacements;
  replacements.ClearRef();
  if (url.SchemeIs(url::kHttpScheme)) {
    replacements.SetSchemeStr(url::kHttpsScheme);
  }
  return url.ReplaceComponents(replacements);
}

}  // namespace

int FindMaterializedPinnedItemTab(TabStripModel* tab_strip,
                                  const std::string& pinned_item_id,
                                  const std::string& active_space_id) {
  if (!tab_strip || pinned_item_id.empty()) {
    return TabStripModel::kNoTab;
  }

  for (int i = 0; i < tab_strip->count(); ++i) {
    content::WebContents* contents = tab_strip->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    if (GetPinnedItemIdForTab(contents) != pinned_item_id) {
      continue;
    }
    if (!TabBelongsToSpace(contents, active_space_id)) {
      continue;
    }
    return i;
  }

  return TabStripModel::kNoTab;
}

void PinAndCreatePinnedItem(TabStripModel* tab_strip,
                            PinnedItemsManager* pinned_items_manager,
                            content::WebContents* contents) {
  if (!contents) {
    return;
  }

  if (tab_strip) {
    const int index = tab_strip->GetIndexOfWebContents(contents);
    if (index != TabStripModel::kNoTab && !tab_strip->IsTabPinned(index)) {
      tab_strip->SetTabPinned(index, true);
    }
  }

  if (!pinned_items_manager || IsPinnedItemTab(contents)) {
    return;
  }

  const std::string url = contents->GetLastCommittedURL().spec();
  if (url.empty()) {
    return;
  }

  std::string item_id = pinned_items_manager->GetPinnedItemIdForUrl(url);
  if (item_id.empty()) {
    item_id = pinned_items_manager->AddPinnedItem(
        url, base::UTF16ToUTF8(contents->GetTitle()));
  }
  if (!item_id.empty()) {
    MarkPinnedItemTab(contents, item_id);
  }
}

void UnpinAndRemovePinnedItem(TabStripModel* tab_strip,
                              PinnedItemsManager* pinned_items_manager,
                              PinnedFoldersManager* pinned_folders_manager,
                              content::WebContents* contents) {
  if (!contents) {
    return;
  }

  if (tab_strip) {
    const int index = tab_strip->GetIndexOfWebContents(contents);
    if (index != TabStripModel::kNoTab && tab_strip->IsTabPinned(index)) {
      tab_strip->SetTabPinned(index, false);
    }
  }

  const std::string item_id = GetPinnedItemIdForTab(contents);
  if (item_id.empty()) {
    return;
  }

  // Unmark before either pref write, not after.  Both writes notify their
  // observers synchronously, and those observers -- AvoraPinnedSectionView::
  // Rebuild() and AvoraSpaceTabFilter::ApplyVisibility() -- read this marker
  // to decide whether the tab still belongs to the pinned section.  Clearing
  // it last left every one of those passes looking at a tab that still
  // claimed to be pinned, and because the marker is WebContents user data,
  // clearing it announces nothing that would run them again: the row stayed
  // on screen, and the tab stayed hidden from the daily list, until some
  // unrelated event happened to rebuild.  The record is gone from prefs
  // either way, so the only thing the old order changed was what the user
  // saw -- an "Unpin Tab" that looked like it did nothing.
  UnmarkPinnedItemTab(contents);

  // Scrub folder membership before removing the record itself, so there is
  // never a window (even a transient in-memory one) where a folder
  // references an id that no longer resolves to anything.
  if (pinned_folders_manager) {
    pinned_folders_manager->RemoveItemFromAllFolders(item_id);
  }
  if (pinned_items_manager) {
    pinned_items_manager->RemovePinnedItem(item_id);
  }
}

bool IsAtPinnedTarget(const GURL& target, const GURL& current) {
  if (!target.is_valid()) {
    return true;  // No target to have left.
  }
  // Nothing committed yet: a row is built the moment its tab is inserted,
  // before the first navigation commits, and a marker that appeared for that
  // frame would flash on every pin the user opens.
  if (!current.is_valid() || current.IsAboutBlank()) {
    return true;
  }
  return CanonicalizeForTargetComparison(target) ==
         CanonicalizeForTargetComparison(current);
}

GURL GetPinnedTargetForTab(PinnedItemsManager* pinned_items_manager,
                           content::WebContents* contents) {
  if (!pinned_items_manager || !contents) {
    return GURL();
  }
  const std::string item_id = GetPinnedItemIdForTab(contents);
  if (item_id.empty()) {
    return GURL();
  }
  return GURL(pinned_items_manager->GetPinnedItemUrlById(item_id));
}

bool HasLeftPinnedTarget(PinnedItemsManager* pinned_items_manager,
                         content::WebContents* contents) {
  const GURL target = GetPinnedTargetForTab(pinned_items_manager, contents);
  if (!target.is_valid()) {
    return false;
  }
  return !IsAtPinnedTarget(target, contents->GetLastCommittedURL());
}

void ReturnTabToPinnedTarget(PinnedItemsManager* pinned_items_manager,
                             content::WebContents* contents) {
  const GURL target = GetPinnedTargetForTab(pinned_items_manager, contents);
  if (!target.is_valid() ||
      IsAtPinnedTarget(target, contents->GetLastCommittedURL())) {
    return;
  }

  // PAGE_TRANSITION_AUTO_BOOKMARK for the same reason every other
  // pinned-item navigation uses it: this is the sidebar acting on a saved
  // destination, not the user typing or following a link, and the omnibox
  // and history should score it that way.
  content::NavigationController::LoadURLParams params(target);
  params.transition_type = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
  contents->GetController().LoadURLWithParams(params);
}

}  // namespace avora
