// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_tab_guid.h"

#include <memory>
#include <utility>

#include "base/supports_user_data.h"
#include "base/uuid.h"
#include "chrome/browser/avora/avora_tab_guid_session_handler.h"
#include "components/sessions/content/content_serialized_navigation_driver.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"

namespace avora {

namespace {

constexpr char kTabGuidWebContentsKey[] = "avora_tab_guid";
constexpr char kTabGuidEntryKey[] = "avora_tab_guid";

class TabGuidWebContentsData : public base::SupportsUserData::Data {
 public:
  explicit TabGuidWebContentsData(std::string guid) : guid_(std::move(guid)) {}
  const std::string& guid() const { return guid_; }

 private:
  std::string guid_;
};

class TabGuidEntryData : public base::SupportsUserData::Data {
 public:
  explicit TabGuidEntryData(std::string guid) : guid_(std::move(guid)) {}
  const std::string& guid() const { return guid_; }

 private:
  std::string guid_;
};

void SetTabGuidOnNavigationEntry(content::NavigationEntry* entry,
                                 const std::string& guid) {
  if (!entry || guid.empty()) {
    return;
  }
  entry->SetUserData(kTabGuidEntryKey,
                     std::make_unique<TabGuidEntryData>(guid));
}

TabGuidWebContentsData* GetWebContentsData(content::WebContents* contents) {
  if (!contents) {
    return nullptr;
  }
  return static_cast<TabGuidWebContentsData*>(
      contents->GetUserData(kTabGuidWebContentsKey));
}

}  // namespace

void RestoreTabGuidOnNavigationEntry(content::NavigationEntry* entry,
                                       const std::string& guid) {
  SetTabGuidOnNavigationEntry(entry, guid);
}

void RegisterTabGuidSessionHandler() {
  sessions::ContentSerializedNavigationDriver::GetInstance()
      ->RegisterExtendedInfoHandler(
          kTabGuidExtendedInfoKey,
          std::make_unique<AvoraTabGuidSessionHandler>());
}

std::string GetTabGuid(content::WebContents* contents) {
  TabGuidWebContentsData* data = GetWebContentsData(contents);
  return data ? data->guid() : std::string();
}

void SetTabGuid(content::WebContents* contents, const std::string& guid) {
  if (!contents || guid.empty()) {
    return;
  }

  contents->SetUserData(kTabGuidWebContentsKey,
                        std::make_unique<TabGuidWebContentsData>(guid));
  SyncTabGuidToAllEntries(contents);
}

std::string GetOrCreateTabGuid(content::WebContents* contents) {
  if (!contents) {
    return std::string();
  }

  if (TabGuidWebContentsData* data = GetWebContentsData(contents)) {
    return data->guid();
  }

  const std::string guid =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  SetTabGuid(contents, guid);
  return guid;
}

std::string GetTabGuidFromNavigationEntry(content::NavigationEntry* entry) {
  if (!entry) {
    return std::string();
  }
  TabGuidEntryData* data =
      static_cast<TabGuidEntryData*>(entry->GetUserData(kTabGuidEntryKey));
  return data ? data->guid() : std::string();
}

std::string FindTabGuidInController(content::NavigationController* controller) {
  if (!controller) {
    return std::string();
  }

  const int count = controller->GetEntryCount();
  if (count == 0) {
    return std::string();
  }

  const int selected = controller->GetCurrentEntryIndex();
  const int pending = controller->GetPendingEntryIndex();
  if (pending != -1) {
    if (content::NavigationEntry* entry = controller->GetPendingEntry()) {
      const std::string guid = GetTabGuidFromNavigationEntry(entry);
      if (!guid.empty()) {
        return guid;
      }
    }
  }

  auto try_index = [&](int index) -> std::string {
    if (index < 0 || index >= count) {
      return std::string();
    }
    return GetTabGuidFromNavigationEntry(controller->GetEntryAtIndex(index));
  };

  if (const std::string guid = try_index(selected); !guid.empty()) {
    return guid;
  }

  for (int offset = 1; offset < count; ++offset) {
    if (const std::string guid = try_index(selected - offset); !guid.empty()) {
      return guid;
    }
    if (const std::string guid = try_index(selected + offset); !guid.empty()) {
      return guid;
    }
  }
  return std::string();
}

void AdoptTabGuidFromController(content::WebContents* contents) {
  if (!contents || !GetTabGuid(contents).empty()) {
    return;
  }

  const std::string guid =
      FindTabGuidInController(&contents->GetController());
  if (guid.empty()) {
    return;
  }
  SetTabGuid(contents, guid);
}

void SyncTabGuidToAllEntries(content::WebContents* contents) {
  if (!contents) {
    return;
  }

  const std::string guid = GetTabGuid(contents);
  if (guid.empty()) {
    return;
  }

  content::NavigationController& controller = contents->GetController();
  if (content::NavigationEntry* pending = controller.GetPendingEntry()) {
    SetTabGuidOnNavigationEntry(pending, guid);
  }
  for (int i = 0; i < controller.GetEntryCount(); ++i) {
    SetTabGuidOnNavigationEntry(controller.GetEntryAtIndex(i), guid);
  }
}

void TransferTabGuid(content::WebContents* old_contents,
                     content::WebContents* new_contents) {
  if (!old_contents || !new_contents || old_contents == new_contents) {
    return;
  }

  const std::string guid = GetTabGuid(old_contents);
  if (guid.empty()) {
    AdoptTabGuidFromController(old_contents);
  }

  const std::string resolved =
      !GetTabGuid(old_contents).empty() ? GetTabGuid(old_contents)
                                        : FindTabGuidInController(
                                              &old_contents->GetController());
  if (resolved.empty()) {
    return;
  }

  SetTabGuid(new_contents, resolved);
}

}  // namespace avora
