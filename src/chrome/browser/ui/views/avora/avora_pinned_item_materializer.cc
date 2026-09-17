// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_pinned_item_materializer.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/avora/avora_pinned_items.h"
#include "chrome/browser/avora/avora_tab_space.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_tab_marker.h"
#include "content/public/browser/web_contents.h"

namespace avora {

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

  if (!pinned_items_manager) {
    return;
  }

  const std::string item_id = GetPinnedItemIdForTab(contents);
  if (item_id.empty()) {
    return;
  }
  pinned_items_manager->RemovePinnedItem(item_id);
  UnmarkPinnedItemTab(contents);
}

}  // namespace avora
