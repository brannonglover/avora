// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_pinned_item_tab_marker.h"

#include <memory>

#include "base/supports_user_data.h"
#include "content/public/browser/web_contents.h"

namespace avora {

namespace {

const char kPinnedItemTabKey[] = "avora_pinned_item_tab";

class PinnedItemTabData : public base::SupportsUserData::Data {
 public:
  explicit PinnedItemTabData(const std::string& id) : id_(id) {}
  ~PinnedItemTabData() override = default;

  const std::string& id() const { return id_; }

 private:
  std::string id_;
};

PinnedItemTabData* GetData(content::WebContents* contents) {
  if (!contents) {
    return nullptr;
  }
  return static_cast<PinnedItemTabData*>(
      contents->GetUserData(kPinnedItemTabKey));
}

}  // namespace

void MarkPinnedItemTab(content::WebContents* contents,
                      const std::string& pinned_item_id) {
  if (!contents) {
    return;
  }
  contents->SetUserData(kPinnedItemTabKey,
                        std::make_unique<PinnedItemTabData>(pinned_item_id));
}

void UnmarkPinnedItemTab(content::WebContents* contents) {
  if (!contents) {
    return;
  }
  contents->RemoveUserData(kPinnedItemTabKey);
}

bool IsPinnedItemTab(content::WebContents* contents) {
  return GetData(contents) != nullptr;
}

std::string GetPinnedItemIdForTab(content::WebContents* contents) {
  PinnedItemTabData* data = GetData(contents);
  return data ? data->id() : std::string();
}

}  // namespace avora
