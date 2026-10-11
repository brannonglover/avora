// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AVORA_AVORA_NAV_HISTORY_H_
#define CHROME_BROWSER_AVORA_AVORA_NAV_HISTORY_H_

#include <stddef.h>

#include <string>
#include <vector>

#include "ui/gfx/image/image.h"
#include "url/gurl.h"

namespace content {
class WebContents;
}  // namespace content

namespace avora {

// Which end of a tab's session history a menu is showing: the entries behind
// the current one, or the ones ahead of it.
enum class NavHistoryDirection { kBack, kForward };

// One entry of a tab's session history, flattened out of Chromium's
// NavigationEntry so Avora's views never touch a content/ type.
struct NavHistoryEntry {
  NavHistoryEntry();
  NavHistoryEntry(const NavHistoryEntry&);
  NavHistoryEntry& operator=(const NavHistoryEntry&);
  NavHistoryEntry(NavHistoryEntry&&);
  NavHistoryEntry& operator=(NavHistoryEntry&&);
  ~NavHistoryEntry();

  // Index into the tab's NavigationController.  Hand it back to
  // GoToNavHistoryEntry() to navigate there.
  int index = 0;

  // Page title, already falling back to a display-formatted URL when the page
  // never supplied one.
  std::u16string title;

  // The URL to show the user, which for error pages and view-source is not the
  // URL that was actually loaded.
  GURL url;

  // The favicon the page last reported.  Empty until one has arrived, so
  // callers need their own fallback glyph.
  gfx::Image favicon;
};

// How many entries a history menu shows before it stops.  Matches the depth
// Chromium's own back/forward menu offers.
inline constexpr size_t kMaxNavHistoryEntries = 12;

// Returns the entries on one side of |contents|' current position, nearest
// first, capped at kMaxNavHistoryEntries.  Empty when there is nowhere to go.
std::vector<NavHistoryEntry> GetNavHistory(content::WebContents* contents,
                                           NavHistoryDirection direction);

// Navigates |contents| to the entry at |index|.  Out-of-range indices — an
// entry pruned while a menu was open, say — are ignored.
void GoToNavHistoryEntry(content::WebContents* contents, int index);

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_NAV_HISTORY_H_
