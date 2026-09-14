// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_tab_space.h"

#include <memory>
#include <utility>

#include "base/supports_user_data.h"
#include "content/public/browser/web_contents.h"

namespace avora {

namespace {

// Key identifying the Space tag among the WebContents' user data.
constexpr char kTabSpaceDataKey[] = "avora_tab_space";

class TabSpaceData : public base::SupportsUserData::Data {
 public:
  explicit TabSpaceData(std::string space_id)
      : space_id_(std::move(space_id)) {}

  const std::string& space_id() const { return space_id_; }
  void set_space_id(std::string space_id) { space_id_ = std::move(space_id); }

 private:
  std::string space_id_;
};

TabSpaceData* GetData(content::WebContents* contents) {
  if (!contents) {
    return nullptr;
  }
  return static_cast<TabSpaceData*>(
      contents->GetUserData(kTabSpaceDataKey));
}

}  // namespace

void SetTabSpaceId(content::WebContents* contents,
                   const std::string& space_id) {
  if (!contents) {
    return;
  }

  if (space_id.empty()) {
    contents->RemoveUserData(kTabSpaceDataKey);
    return;
  }

  if (TabSpaceData* data = GetData(contents)) {
    data->set_space_id(space_id);
    return;
  }

  contents->SetUserData(kTabSpaceDataKey,
                        std::make_unique<TabSpaceData>(space_id));
}

std::string GetTabSpaceId(content::WebContents* contents) {
  if (TabSpaceData* data = GetData(contents)) {
    return data->space_id();
  }
  return std::string();
}

bool TabBelongsToSpace(content::WebContents* contents,
                       const std::string& active_space_id) {
  const std::string tab_space_id = GetTabSpaceId(contents);
  if (tab_space_id.empty()) {
    return true;
  }
  return tab_space_id == active_space_id;
}

}  // namespace avora
