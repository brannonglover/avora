// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_SYNC_AVORA_TAB_SNAPSHOT_H_
#define CHROME_BROWSER_AVORA_SYNC_AVORA_TAB_SNAPSHOT_H_

#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "base/values.h"
#include "url/gurl.h"

namespace avora::sync {

// One open tab as the browser sees it, before filtering. Collected from every
// window of the profile (see avora_tab_sync_watcher.h).
struct OpenTab {
  GURL url;
  std::u16string title;
  bool pinned = false;
  // Display name of the Space the tab lives in; empty for an untagged tab.
  std::string space;
  // Null when the tab has never been shown.
  base::Time last_active;
  // The Avora tab GUID. A Space's daily tabs are copied into every window of
  // the Space, and the copies of one logical tab share it; empty when Avora
  // has not claimed the tab.
  std::string guid;
};

// Whether a tab with `url` belongs in the synced snapshot. Only http(s) pages
// are: chrome://, file://, about: and the like mean nothing on another device.
bool IsSyncableUrl(const GURL& url);

// The `tabs` array of a public.devices row, in the shape docs/SYNC.md defines:
// {url, title, pinned, space?, lastActiveAt?} per tab. Leaves out unsyncable
// URLs, strips any username and password from the rest, and folds the copies
// of one logical tab (same GUID) into one entry, at the position of the first
// copy: the most recently active copy supplies the page, and the entry is
// pinned if any copy is. Tabs without a GUID are kept as they are.
base::ListValue BuildTabsList(const std::vector<OpenTab>& tabs);

// The JSON body of the PostgREST upsert for this device's row.
std::string BuildDeviceUpsertBody(std::string_view device_id,
                                  std::string_view device_name,
                                  std::string_view platform,
                                  base::ListValue tabs);

// The `platform` value for this build: "macos", "windows" or "linux".
const char* CurrentPlatform();

}  // namespace avora::sync

#endif  // CHROME_BROWSER_AVORA_SYNC_AVORA_TAB_SNAPSHOT_H_
