// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_side_by_side.h"

#include "chrome/browser/avora/avora_tab_space.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_favorite_tab_marker.h"
#include "chrome/browser/ui/views/avora/avora_link_preview_tab_marker.h"
#include "chrome/browser/ui/views/avora/avora_live_folder_tab_marker.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_tab_marker.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/range/range.h"

namespace avora {

bool CanPutTabSideBySide(tabs::TabInterface* tab) {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!contents || tab->IsPinned() || tab->IsSplit() ||
      GetTabSpaceId(contents).empty()) {
    return false;
  }
  return !IsFavoriteTab(contents) && !IsPinnedItemTab(contents) &&
         !IsLinkPreviewTab(contents) && !IsLiveFolderTab(contents);
}

bool CanPutTabsSideBySide(tabs::TabInterface* a, tabs::TabInterface* b) {
  return a != b && CanPutTabSideBySide(a) && CanPutTabSideBySide(b) &&
         GetTabSpaceId(a->GetContents()) == GetTabSpaceId(b->GetContents());
}

std::vector<tabs::TabInterface*> GetSideBySidePartners(
    const TabStripModel& model,
    tabs::TabInterface* tab) {
  std::vector<tabs::TabInterface*> partners;
  for (int i = 0; i < model.count(); ++i) {
    tabs::TabInterface* candidate = model.GetTabAtIndex(i);
    if (!CanPutTabsSideBySide(tab, candidate)) {
      continue;
    }
    if (candidate->IsActivated()) {
      partners.insert(partners.begin(), candidate);
    } else {
      partners.push_back(candidate);
    }
  }
  return partners;
}

void PutTabsSideBySide(TabStripModel& model,
                       tabs::TabInterface* anchor,
                       tabs::TabInterface* incoming,
                       split_tabs::SplitTabCreatedSource source) {
  if (!CanPutTabsSideBySide(anchor, incoming)) {
    return;
  }
  const int anchor_index = model.GetIndexOfTab(anchor);
  const int incoming_index = model.GetIndexOfTab(incoming);
  if (anchor_index == TabStripModel::kNoTab ||
      incoming_index == TabStripModel::kNoTab) {
    return;
  }

  // AddToNewSplit() keeps the tabs in strip order, whichever way the user
  // brought them together.
  const split_tabs::SplitTabId split_id = model.AddToNewSplit(
      {anchor_index, incoming_index},
      split_tabs::SplitTabVisualData(split_tabs::SplitTabLayout::kSideBySide),
      source);
  if (incoming_index < anchor_index) {
    model.ReverseTabsInSplit(split_id);
  }

  // Neither tab has to be the active one -- both can be background tabs picked
  // from a context menu -- and a pair only shows while one of its tabs is
  // active.
  model.ActivateTabAt(model.GetIndexOfTab(incoming));
}

int InsertionIndexOutsideSplit(const TabStripModel& model, int index) {
  if (!model.ContainsIndex(index)) {
    return index;
  }
  const std::optional<split_tabs::SplitTabId> split =
      model.GetTabAtIndex(index)->GetSplit();
  if (!split.has_value()) {
    return index;
  }
  const gfx::Range range = model.GetSplitData(*split)->GetIndexRange();
  return index > static_cast<int>(range.start()) ? range.end() : index;
}

}  // namespace avora
