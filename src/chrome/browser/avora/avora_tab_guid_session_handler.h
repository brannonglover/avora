// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_TAB_GUID_SESSION_HANDLER_H_
#define CHROME_BROWSER_AVORA_AVORA_TAB_GUID_SESSION_HANDLER_H_

#include "components/sessions/content/extended_info_handler.h"

namespace avora {

// Persists Avora tab GUIDs through SerializedNavigationEntry::extended_info_map.
class AvoraTabGuidSessionHandler : public sessions::ExtendedInfoHandler {
 public:
  AvoraTabGuidSessionHandler();
  AvoraTabGuidSessionHandler(const AvoraTabGuidSessionHandler&) = delete;
  AvoraTabGuidSessionHandler& operator=(const AvoraTabGuidSessionHandler&) =
      delete;
  ~AvoraTabGuidSessionHandler() override;

  std::string GetExtendedInfo(content::NavigationEntry* entry) const override;
  void RestoreExtendedInfo(const std::string& info,
                           content::NavigationEntry* entry) override;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_TAB_GUID_SESSION_HANDLER_H_
