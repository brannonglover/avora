// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_live_folder_provider.h"

#include <utility>

namespace avora {

// static
LiveFolderFetchResult LiveFolderFetchResult::Success(
    std::vector<LiveFolderItem> items) {
  LiveFolderFetchResult result;
  result.status = LiveFolderFetchStatus::kSuccess;
  result.items = std::move(items);
  return result;
}

// static
LiveFolderFetchResult LiveFolderFetchResult::Error(
    LiveFolderFetchStatus status,
    const std::string& message) {
  LiveFolderFetchResult result;
  result.status = status;
  result.error_message = message;
  return result;
}

}  // namespace avora
