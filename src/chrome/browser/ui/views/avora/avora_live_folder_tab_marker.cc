// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_live_folder_tab_marker.h"

#include <memory>

#include "base/no_destructor.h"
#include "base/supports_user_data.h"
#include "content/public/browser/web_contents.h"

namespace avora {

namespace {

const char kLiveFolderTabKey[] = "avora_live_folder_tab";

class LiveFolderTabData : public base::SupportsUserData::Data {
 public:
  explicit LiveFolderTabData(const LiveFolderTabKey& key) : key_(key) {}
  ~LiveFolderTabData() override = default;

  const LiveFolderTabKey& key() const { return key_; }

 private:
  LiveFolderTabKey key_;
};

LiveFolderTabData* GetData(content::WebContents* contents) {
  if (!contents) {
    return nullptr;
  }
  return static_cast<LiveFolderTabData*>(
      contents->GetUserData(kLiveFolderTabKey));
}

using MarkChangedCallbackList =
    base::RepeatingCallbackList<void(content::WebContents*)>;

MarkChangedCallbackList& GetMarkChangedCallbacks() {
  static base::NoDestructor<MarkChangedCallbackList> callbacks;
  return *callbacks;
}

void SetMark(content::WebContents* contents, const LiveFolderTabKey& key) {
  contents->SetUserData(kLiveFolderTabKey,
                        std::make_unique<LiveFolderTabData>(key));
}

}  // namespace

void MarkLiveFolderTab(content::WebContents* contents,
                       const LiveFolderTabKey& key) {
  if (!contents) {
    return;
  }
  if (const LiveFolderTabData* data = GetData(contents);
      data && data->key() == key) {
    return;
  }
  SetMark(contents, key);
  GetMarkChangedCallbacks().Notify(contents);
}

void UnmarkLiveFolderTab(content::WebContents* contents) {
  if (!GetData(contents)) {
    return;
  }
  contents->RemoveUserData(kLiveFolderTabKey);
  GetMarkChangedCallbacks().Notify(contents);
}

bool IsLiveFolderTab(content::WebContents* contents) {
  return GetData(contents) != nullptr;
}

std::optional<LiveFolderTabKey> GetLiveFolderTabKey(
    content::WebContents* contents) {
  const LiveFolderTabData* data = GetData(contents);
  if (!data) {
    return std::nullopt;
  }
  return data->key();
}

base::CallbackListSubscription AddLiveFolderTabMarkChangedCallback(
    base::RepeatingCallback<void(content::WebContents*)> callback) {
  return GetMarkChangedCallbacks().Add(std::move(callback));
}

void TransferLiveFolderTabMark(content::WebContents* old_contents,
                               content::WebContents* new_contents) {
  const LiveFolderTabData* data = GetData(old_contents);
  if (!data || !new_contents) {
    return;
  }
  SetMark(new_contents, data->key());
}

}  // namespace avora
