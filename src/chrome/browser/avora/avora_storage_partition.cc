// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_storage_partition.h"

#include "chrome/browser/avora/avora_profile.h"
#include "content/public/browser/site_instance.h"
#include "url/gurl.h"

namespace avora {

bool UsesDefaultPartition(const BrowserProfile& profile) {
  return profile.partition.empty();
}

content::StoragePartitionConfig GetPartitionConfigForProfile(
    content::BrowserContext* browser_context,
    const BrowserProfile& profile) {
  if (UsesDefaultPartition(profile)) {
    return content::StoragePartitionConfig::CreateDefault(browser_context);
  }
  return content::StoragePartitionConfig::Create(
      browser_context, kAvoraPartitionDomain, profile.partition,
      /*in_memory=*/false);
}

scoped_refptr<content::SiteInstance> CreateSiteInstanceForProfile(
    content::BrowserContext* browser_context,
    const BrowserProfile& profile,
    const GURL& url) {
  if (UsesDefaultPartition(profile)) {
    return nullptr;
  }
  return content::SiteInstance::CreateForFixedStoragePartition(
      browser_context, url,
      GetPartitionConfigForProfile(browser_context, profile));
}

}  // namespace avora
