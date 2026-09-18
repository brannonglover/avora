// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_DOWNLOADS_BUTTON_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_DOWNLOADS_BUTTON_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/button.h"

class BrowserWindowInterface;
class Profile;

namespace gfx {
class Canvas;
}

namespace avora {

// The downloads button: the leading slot of the Space bar, opposite the "+".
//
// At rest it is the Lucide download glyph.  While downloads are running it
// carries a progress ring around the glyph, which is the only thing in the
// window that says a download is in flight — Avora has no toolbar for
// Chromium's download bubble to live in.  The button never opens itself; the
// ring is the notification, and clicking it opens the downloads panel.
class DownloadsButton : public views::Button,
                        public content::DownloadManager::Observer,
                        public download::DownloadItem::Observer {
  METADATA_HEADER(DownloadsButton, views::Button)

 public:
  DownloadsButton(Profile* profile, BrowserWindowInterface* browser);
  DownloadsButton(const DownloadsButton&) = delete;
  DownloadsButton& operator=(const DownloadsButton&) = delete;
  ~DownloadsButton() override;

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override;

  // content::DownloadManager::Observer:
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void ManagerGoingDown(content::DownloadManager* manager) override;

  // download::DownloadItem::Observer:
  void OnDownloadUpdated(download::DownloadItem* item) override;
  void OnDownloadRemoved(download::DownloadItem* item) override;
  void OnDownloadDestroyed(download::DownloadItem* item) override;

 private:
  void OnPressed();

  // Re-reads the manager: which downloads are running, and how far along they
  // are between them.  Repaints only when the picture actually changed, since
  // an active download updates many times a second.
  void RefreshProgress();

  void StopObservingItems();

  raw_ptr<Profile> profile_;
  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<content::DownloadManager> manager_ = nullptr;

  std::vector<download::DownloadItem*> observed_items_;

  // How many downloads are running, and their combined completion in [0,1].
  // A negative fraction means downloads are running whose size is unknown, so
  // no meaningful arc can be drawn.
  int active_count_ = 0;
  double progress_ = -1.0;

  base::WeakPtrFactory<DownloadsButton> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_DOWNLOADS_BUTTON_H_
