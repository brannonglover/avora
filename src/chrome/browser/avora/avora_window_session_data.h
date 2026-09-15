// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_WINDOW_SESSION_DATA_H_
#define CHROME_BROWSER_AVORA_AVORA_WINDOW_SESSION_DATA_H_

#include <string>

#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"

namespace ui {
class UnownedUserDataHost;
}  // namespace ui

namespace avora {

// Session-restore data for an Avora window, stored on
// BrowserWindowInterface::GetUnownedUserDataHost().
//
// Holds the durable window GUID and the active Space ID.  Readable from any
// code path that has a BrowserWindowInterface*, including
// SessionServiceBase::BuildCommandsForBrowser().
//
// Lifetime is managed by the owning BrowserView through a
// ScopedUnownedUserData member; when that member is destroyed the entry is
// automatically removed from the host.
class AvoraWindowSessionData {
 public:
  DECLARE_USER_DATA(AvoraWindowSessionData);

  // Session extra-data keys (must remain stable across releases).
  static constexpr char kWindowGuidKey[] = "avora_window_guid";
  static constexpr char kActiveSpaceIdKey[] = "avora_active_space_id";

  AvoraWindowSessionData();
  ~AvoraWindowSessionData();

  const std::string& window_guid() const { return window_guid_; }
  void set_window_guid(const std::string& guid) { window_guid_ = guid; }

  const std::string& active_space_id() const { return active_space_id_; }
  void set_active_space_id(const std::string& id) { active_space_id_ = id; }

 private:
  std::string window_guid_;
  std::string active_space_id_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_WINDOW_SESSION_DATA_H_
