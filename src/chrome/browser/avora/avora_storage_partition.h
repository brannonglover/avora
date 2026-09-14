// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_STORAGE_PARTITION_H_
#define CHROME_BROWSER_AVORA_AVORA_STORAGE_PARTITION_H_

#include <string>

#include "base/memory/scoped_refptr.h"
#include "content/public/browser/storage_partition_config.h"

class GURL;

namespace content {
class BrowserContext;
class SiteInstance;
}  // namespace content

namespace avora {

struct BrowserProfile;

// All Avora identity partitions share one partition domain.  StoragePartition
// requires the domain to match [a-z]*, so the per-identity part is carried in
// the partition *name* instead.
inline constexpr char kAvoraPartitionDomain[] = "avora";

// True when |profile| maps to Chromium's default StoragePartition.  The default
// identity does, which is what allows a fresh install (or an upgrade from a
// pre-Spaces build) to keep its existing cookies and logins.
bool UsesDefaultPartition(const BrowserProfile& profile);

// The StoragePartitionConfig isolating |profile|'s cookies, localStorage,
// IndexedDB, service workers, and permissions.
content::StoragePartitionConfig GetPartitionConfigForProfile(
    content::BrowserContext* browser_context,
    const BrowserProfile& profile);

// A SiteInstance pinned to |profile|'s partition for the lifetime of the tab,
// suitable for passing to WebContents::CreateParams::site_instance.
//
// Returns nullptr when |profile| uses the default partition; callers should
// then create the WebContents the ordinary way, since Chromium requires a
// non-default config for a fixed-partition SiteInstance.
scoped_refptr<content::SiteInstance> CreateSiteInstanceForProfile(
    content::BrowserContext* browser_context,
    const BrowserProfile& profile,
    const GURL& url);

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_STORAGE_PARTITION_H_
