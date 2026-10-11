// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/avora/avora_nav_history.h"

#include <utility>

#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"

namespace avora {

NavHistoryEntry::NavHistoryEntry() = default;
NavHistoryEntry::NavHistoryEntry(const NavHistoryEntry&) = default;
NavHistoryEntry& NavHistoryEntry::operator=(const NavHistoryEntry&) = default;
NavHistoryEntry::NavHistoryEntry(NavHistoryEntry&&) = default;
NavHistoryEntry& NavHistoryEntry::operator=(NavHistoryEntry&&) = default;
NavHistoryEntry::~NavHistoryEntry() = default;

std::vector<NavHistoryEntry> GetNavHistory(content::WebContents* contents,
                                           NavHistoryDirection direction) {
  std::vector<NavHistoryEntry> entries;
  if (!contents) {
    return entries;
  }

  content::NavigationController& controller = contents->GetController();
  const int current = controller.GetCurrentEntryIndex();
  if (current < 0) {
    return entries;
  }

  // Walk away from the current entry, so the first row of a menu is always the
  // one step the plain button click would have taken.
  const int step = direction == NavHistoryDirection::kBack ? -1 : 1;
  for (int i = current + step;
       i >= 0 && i < controller.GetEntryCount() &&
       entries.size() < kMaxNavHistoryEntries;
       i += step) {
    content::NavigationEntry* entry = controller.GetEntryAtIndex(i);
    if (!entry) {
      continue;
    }
    NavHistoryEntry out;
    out.index = i;
    out.title = entry->GetTitleForDisplay();
    out.url = entry->GetVirtualURL();
    // An entry only carries a favicon once one has actually arrived for it in
    // this tab, this run; otherwise the status holds a placeholder that would
    // be worse than the caller's own fallback.  Callers fill the gap from the
    // favicon service.
    const content::FaviconStatus& favicon = entry->GetFavicon();
    if (favicon.valid) {
      out.favicon = favicon.image;
    }
    entries.push_back(std::move(out));
  }
  return entries;
}

void GoToNavHistoryEntry(content::WebContents* contents, int index) {
  if (!contents) {
    return;
  }
  content::NavigationController& controller = contents->GetController();
  if (index < 0 || index >= controller.GetEntryCount()) {
    return;
  }
  controller.GoToIndex(index);
}

}  // namespace avora
