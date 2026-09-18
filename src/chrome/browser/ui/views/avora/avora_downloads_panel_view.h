// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_DOWNLOADS_PANEL_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_DOWNLOADS_PANEL_VIEW_H_

#include <map>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class BrowserWindowInterface;
class Profile;

namespace views {
class Label;
}  // namespace views

namespace avora {

class DownloadRowView;

// The contents of the downloads panel: this profile's downloads, newest first,
// with the directory they land in named at the foot of the panel.  Shown in a
// bubble anchored to the downloads button in the Space bar (see
// ShowDownloadsPanel below).
//
// The panel is live while it is open.  It observes the DownloadManager for
// downloads arriving and leaving, and each listed item for progress, so a
// download that finishes while the user is looking at the panel updates in
// place instead of going stale.
class DownloadsPanelView : public views::View,
                           public content::DownloadManager::Observer,
                           public download::DownloadItem::Observer {
  METADATA_HEADER(DownloadsPanelView, views::View)

 public:
  // Panel geometry.  Wider than the sidebar's other bubbles because a row
  // carries a filename, which elides badly when narrow.
  static constexpr int kPanelWidth = 340;
  static constexpr int kListMaxHeight = 380;

  // The list is capped: the panel is a glance at recent activity, not the full
  // history that chrome://downloads holds.
  static constexpr size_t kMaxRows = 25;

  DownloadsPanelView(Profile* profile, BrowserWindowInterface* browser);
  DownloadsPanelView(const DownloadsPanelView&) = delete;
  DownloadsPanelView& operator=(const DownloadsPanelView&) = delete;
  ~DownloadsPanelView() override;

  // content::DownloadManager::Observer:
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void ManagerGoingDown(content::DownloadManager* manager) override;

  // download::DownloadItem::Observer:
  void OnDownloadUpdated(download::DownloadItem* item) override;
  void OnDownloadRemoved(download::DownloadItem* item) override;
  void OnDownloadDestroyed(download::DownloadItem* item) override;

 private:
  // Tears the list down and builds it again from the manager's current set of
  // downloads.  Used when the set itself changes; a change within one download
  // refreshes that row alone (see OnDownloadUpdated).
  void RebuildList();

  // RebuildList() on a fresh stack.  Download notifications arrive while an
  // item is being torn down and while a row's own button handler is still on
  // the stack, neither of which can safely delete views.
  void ScheduleRebuild();

  // Downloads to show, newest first and capped at kMaxRows.
  std::vector<download::DownloadItem*> GetVisibleDownloads();

  void StopObservingItems();

  // Takes every finished download out of the list.  The files themselves are
  // left alone: this clears the list, it does not delete anything on disk.
  void ClearFinished();

  // Opens chrome://downloads, which holds the full history this panel's list
  // is only the recent end of.
  void ShowAllDownloads();

  raw_ptr<Profile> profile_;

  // The window whose downloads these are, used to open the full history.
  // Null leaves the panel without that row rather than failing.
  raw_ptr<BrowserWindowInterface> browser_;

  raw_ptr<content::DownloadManager> manager_ = nullptr;

  raw_ptr<views::View> list_ = nullptr;
  raw_ptr<views::View> empty_label_ = nullptr;
  raw_ptr<views::Label> footer_label_ = nullptr;
  raw_ptr<views::View> clear_button_ = nullptr;

  // Rows by the download they show, so a progress update can find its row
  // without walking the list.
  std::map<download::DownloadItem*, raw_ptr<DownloadRowView>> rows_;

  // The items this panel holds an observation on, so they can all be dropped
  // before the list is rebuilt.
  std::vector<download::DownloadItem*> observed_items_;

  base::WeakPtrFactory<DownloadsPanelView> weak_factory_{this};
};

// Opens the downloads panel in a bubble anchored to |anchor|, the downloads
// button in the Space bar.  Closes on click-outside or Escape like the
// sidebar's other bubbles.
void ShowDownloadsPanel(views::View* anchor,
                        Profile* profile,
                        BrowserWindowInterface* browser);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_DOWNLOADS_PANEL_VIEW_H_
