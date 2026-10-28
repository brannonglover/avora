// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_DOWNLOADS_BUTTON_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_DOWNLOADS_BUTTON_H_

#include <map>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/geometry/point.h"
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
// carries a progress ring around the glyph — a spinning arc when the size is
// unknown — which is the only thing in the window that says a download is in
// flight: Avora has no toolbar for Chromium's download bubble to live in.
//
// A download started from this window announces itself by throwing a file
// chip from the pointer into the button (see DownloadFlightView), and the
// button pulses as it lands.  The button never opens itself; the flight and
// the ring are the notification, and clicking it opens the downloads panel.
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

  // views::Button (as the delegate of |land_pulse_|):
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

 private:
  void OnPressed();

  // Re-reads the manager: which downloads are running, and how far along they
  // are between them.  Repaints only when the picture actually changed, since
  // an active download updates many times a second.
  void RefreshProgress();

  void StopObservingItems();

  // Whether |item| was started from the window this button sits in.  Every
  // window has its own button; only the one the user clicked in should play
  // the flight.
  bool StartedInThisWindow(download::DownloadItem* item) const;

  // Launches the flight for |item| once its file has a name, which is after
  // the Save As sheet (if any) has been answered: a download the user backs
  // out of there never flies.
  void MaybeLaunchFlight(download::DownloadItem* item);

  // Throws the chip from |start|, in root-view coordinates, into this button.
  void LaunchFlight(const gfx::Point& start);

  // The chip has reached the button.
  void OnFlightLanded();

  // Runs the spinner while a download of unknown size is in flight, and stops
  // it otherwise so an idle window draws nothing.
  void UpdateSpinner();

  raw_ptr<Profile> profile_;
  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<content::DownloadManager> manager_ = nullptr;

  std::vector<download::DownloadItem*> observed_items_;

  // How many downloads are running, and their combined completion in [0,1].
  // A negative fraction means downloads are running whose size is unknown, so
  // no meaningful arc can be drawn.
  int active_count_ = 0;
  double progress_ = -1.0;

  // Downloads waiting on a filename before their flight, with where the
  // pointer was (root-view coordinates) when each one started.
  std::map<download::DownloadItem*, gfx::Point> pending_flights_;

  // The pulse played as a chip lands.
  gfx::LinearAnimation land_pulse_{this};

  // Rotation of the indeterminate arc, in degrees.
  float spinner_angle_ = 0.0f;
  base::RepeatingTimer spinner_timer_;

  base::WeakPtrFactory<DownloadsButton> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_DOWNLOADS_BUTTON_H_
