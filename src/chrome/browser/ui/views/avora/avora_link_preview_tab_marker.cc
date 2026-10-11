// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_link_preview_tab_marker.h"

#include <memory>

#include "base/supports_user_data.h"
#include "content/public/browser/web_contents.h"

namespace avora {

namespace {

const char kLinkPreviewTabKey[] = "avora_link_preview_tab_marker";

class LinkPreviewTabData : public base::SupportsUserData::Data {
 public:
  explicit LinkPreviewTabData(tabs::TabHandle host) : host_(host) {}
  ~LinkPreviewTabData() override = default;

  tabs::TabHandle host() const { return host_; }

 private:
  tabs::TabHandle host_;
};

LinkPreviewTabData* GetData(content::WebContents* contents) {
  if (!contents) {
    return nullptr;
  }
  return static_cast<LinkPreviewTabData*>(
      contents->GetUserData(kLinkPreviewTabKey));
}

}  // namespace

void MarkLinkPreviewTab(content::WebContents* contents, tabs::TabHandle host) {
  if (!contents) {
    return;
  }
  contents->SetUserData(kLinkPreviewTabKey,
                        std::make_unique<LinkPreviewTabData>(host));
}

void UnmarkLinkPreviewTab(content::WebContents* contents) {
  if (!contents) {
    return;
  }
  contents->RemoveUserData(kLinkPreviewTabKey);
}

bool IsLinkPreviewTab(content::WebContents* contents) {
  return GetData(contents) != nullptr;
}

content::WebContents* GetLinkPreviewHostTab(content::WebContents* contents) {
  LinkPreviewTabData* data = GetData(contents);
  if (!data) {
    return nullptr;
  }
  tabs::TabInterface* host = data->host().Get();
  return host ? host->GetContents() : nullptr;
}

}  // namespace avora
