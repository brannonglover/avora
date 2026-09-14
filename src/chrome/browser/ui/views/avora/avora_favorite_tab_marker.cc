// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_favorite_tab_marker.h"

#include <memory>

#include "base/supports_user_data.h"
#include "content/public/browser/web_contents.h"

namespace avora {

namespace {

const char kFavoriteTabKey[] = "avora_favorite_tab";

class FavoriteTabData : public base::SupportsUserData::Data {
 public:
  FavoriteTabData(const std::string& id, bool was_pinned)
      : id_(id), was_pinned_(was_pinned) {}
  ~FavoriteTabData() override = default;

  const std::string& id() const { return id_; }
  bool was_pinned() const { return was_pinned_; }

 private:
  std::string id_;
  bool was_pinned_;
};

FavoriteTabData* GetData(content::WebContents* contents) {
  if (!contents) {
    return nullptr;
  }
  return static_cast<FavoriteTabData*>(
      contents->GetUserData(kFavoriteTabKey));
}

}  // namespace

void MarkFavoriteTab(content::WebContents* contents,
                     const std::string& favorite_id,
                     bool was_pinned) {
  if (!contents) {
    return;
  }
  contents->SetUserData(
      kFavoriteTabKey,
      std::make_unique<FavoriteTabData>(favorite_id, was_pinned));
}

void UnmarkFavoriteTab(content::WebContents* contents) {
  if (!contents) {
    return;
  }
  contents->RemoveUserData(kFavoriteTabKey);
}

bool IsFavoriteTab(content::WebContents* contents) {
  return GetData(contents) != nullptr;
}

std::string GetFavoriteIdForTab(content::WebContents* contents) {
  FavoriteTabData* data = GetData(contents);
  return data ? data->id() : std::string();
}

bool WasPinnedBeforeFavorite(content::WebContents* contents) {
  FavoriteTabData* data = GetData(contents);
  return data && data->was_pinned();
}

}  // namespace avora
