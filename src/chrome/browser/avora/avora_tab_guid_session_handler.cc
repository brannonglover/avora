// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_tab_guid_session_handler.h"

#include "chrome/browser/avora/avora_tab_guid.h"
#include "content/public/browser/navigation_entry.h"

namespace avora {

AvoraTabGuidSessionHandler::AvoraTabGuidSessionHandler() = default;
AvoraTabGuidSessionHandler::~AvoraTabGuidSessionHandler() = default;

std::string AvoraTabGuidSessionHandler::GetExtendedInfo(
    content::NavigationEntry* entry) const {
  return GetTabGuidFromNavigationEntry(entry);
}

void AvoraTabGuidSessionHandler::RestoreExtendedInfo(
    const std::string& info,
    content::NavigationEntry* entry) {
  RestoreTabGuidOnNavigationEntry(entry, info);
}

}  // namespace avora
