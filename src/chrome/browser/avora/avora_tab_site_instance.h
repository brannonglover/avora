// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_TAB_SITE_INSTANCE_H_
#define CHROME_BROWSER_AVORA_AVORA_TAB_SITE_INSTANCE_H_

#include <string>

#include "base/memory/scoped_refptr.h"
#include "url/gurl.h"

class BrowserWindowInterface;
class Profile;

namespace content {
class SiteInstance;
}  // namespace content

namespace avora {

// Returns the SiteInstance a newly created tab should use, based on the Space
// that |browser| is currently showing.
//
// Identity is window-local: the Space comes from AvoraWindowSessionData on the
// window's UnownedUserDataHost, so two windows sitting on different Spaces put
// their new tabs in different storage partitions.  When that data is missing
// (a window created before the Avora views exist, or a non-Avora surface) this
// falls back to the global active Space.
//
// When the resolved Space's BrowserProfile maps to Chromium's default
// StoragePartition, this delegates to tab_util::GetSiteInstanceForNewTab so
// the existing-user path is unchanged.  Otherwise the tab is pinned to that
// profile's isolated partition via CreateSiteInstanceForProfile.
scoped_refptr<content::SiteInstance> GetSiteInstanceForNewAvoraTab(
    BrowserWindowInterface* browser,
    const GURL& url);

// Returns the SiteInstance for a new tab in an explicitly named Space.
// |space_id| may be empty, in which case the global active Space is used.
// Used by the identity migrator, which needs a partition for a specific Space
// rather than for whichever Space a window happens to be showing.
scoped_refptr<content::SiteInstance> GetSiteInstanceForSpace(
    Profile* profile,
    const std::string& space_id,
    const GURL& url);

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_TAB_SITE_INSTANCE_H_
