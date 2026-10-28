// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_DOWNLOADS_PANEL_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_DOWNLOADS_PANEL_VIEW_H_

#include <map>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
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

// One entry of the download folder, as read off disk for the panel.
struct DownloadsFolderEntry {
  base::FilePath path;
  bool is_directory = false;
  int64_t size = 0;
  base::Time modified;
};

// What a scan of the download folder found: its entries newest first, capped
// at DownloadsPanelView::kMaxFileRows, and how many there were in all.
struct DownloadsFolderListing {
  DownloadsFolderListing();
  DownloadsFolderListing(DownloadsFolderListing&&);
  DownloadsFolderListing& operator=(DownloadsFolderListing&&);
  ~DownloadsFolderListing();

  bool readable = false;
  size_t total = 0;
  std::vector<DownloadsFolderEntry> entries;
};

// Reads |dir| the way Finder shows it: no hidden files, and no Chromium
// partial files (those are the downloads still running, which the panel lists
// from the DownloadManager instead).  Blocks on disk, so it runs on the thread
// pool.  Exposed for tests.
DownloadsFolderListing ListDownloadsFolder(const base::FilePath& dir);

// The contents of the downloads panel, in two parts:
//
//  * Downloads still in flight, or stopped short (paused, failed), from this
//    profile's DownloadManager.  These are live: the panel observes each one,
//    so progress ticks in place.
//  * Everything in the download folder (normally ~/Downloads), newest first,
//    read off disk when the panel opens and again whenever a download lands.
//
// Shown in a bubble anchored to the downloads button in the Space bar (see
// ShowDownloadsPanel below).
class DownloadsPanelView : public views::View,
                           public content::DownloadManager::Observer,
                           public download::DownloadItem::Observer {
  METADATA_HEADER(DownloadsPanelView, views::View)

 public:
  // Panel geometry.  Wider than the sidebar's other bubbles because a row
  // carries a filename, which elides badly when narrow.
  static constexpr int kPanelWidth = 340;
  static constexpr int kListMaxHeight = 440;

  // Running downloads are few; this only guards against a runaway page.
  static constexpr size_t kMaxRows = 25;

  // The folder list stops here, with a row that opens the rest in Finder: a
  // Downloads folder can hold thousands of files, and a row is a view.
  static constexpr size_t kMaxFileRows = 300;

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
  // Tears the in-flight list down and builds it again from the manager's
  // current set of downloads.  Used when the set itself changes; a change
  // within one download refreshes that row alone (see OnDownloadUpdated).
  void RebuildList();

  // RebuildList() on a fresh stack.  Download notifications arrive while an
  // item is being torn down and while a row's own button handler is still on
  // the stack, neither of which can safely delete views.
  void ScheduleRebuild();

  // Downloads for the in-flight list, newest first and capped at kMaxRows.
  // A finished download is not among them: its file is in the folder list.
  std::vector<download::DownloadItem*> GetVisibleDownloads();

  void StopObservingItems();

  // Reads the download folder again.  A scan already running is not doubled
  // up; it is followed by one more once it finishes.
  void ScanFolder();
  void OnFolderScanned(DownloadsFolderListing listing);

  // Opens the download folder in Finder.
  void OpenFolder();

  // Opens chrome://downloads, which holds the full download history.
  void ShowAllDownloads();

  // The bubble is sized to its contents at creation, so a list that grew or
  // shrank has to push a new size out to the widget.
  void ResizeBubble();

  raw_ptr<Profile> profile_;

  // The window whose downloads these are, used to open the full history.
  // Null leaves the panel without that row rather than failing.
  raw_ptr<BrowserWindowInterface> browser_;

  raw_ptr<content::DownloadManager> manager_ = nullptr;

  // The folder the files list shows: where downloads are saved.
  base::FilePath folder_;

  raw_ptr<views::View> active_header_ = nullptr;
  raw_ptr<views::View> list_ = nullptr;
  raw_ptr<views::Label> files_header_ = nullptr;
  raw_ptr<views::View> files_list_ = nullptr;
  raw_ptr<views::Label> files_status_ = nullptr;

  // Rows by the download they show, so a progress update can find its row
  // without walking the list.
  std::map<download::DownloadItem*, raw_ptr<DownloadRowView>> rows_;

  // The items this panel holds an observation on, so they can all be dropped
  // before the list is rebuilt.
  std::vector<download::DownloadItem*> observed_items_;

  bool scan_running_ = false;
  bool rescan_wanted_ = false;

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
