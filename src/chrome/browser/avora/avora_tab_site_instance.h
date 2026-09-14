// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_TAB_SITE_INSTANCE_H_
#define CHROME_BROWSER_AVORA_AVORA_TAB_SITE_INSTANCE_H_

#include "base/memory/scoped_refptr.h"
#include "url/gurl.h"

class Profile;

namespace content {
class SiteInstance;
}  // namespace content

namespace avora {

// Returns the SiteInstance a newly created tab in the active Space should use.
//
// When the active Space's BrowserProfile maps to Chromium's default
// StoragePartition, this delegates to tab_util::GetSiteInstanceForNewTab so
// the existing-user path is unchanged.  Otherwise the tab is pinned to that
// profile's isolated partition via CreateSiteInstanceForProfile.
scoped_refptr<content::SiteInstance> GetSiteInstanceForNewAvoraTab(
    Profile* profile,
    const GURL& url);

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_TAB_SITE_INSTANCE_H_
