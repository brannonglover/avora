// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_UPDATER_H_
#define CHROME_BROWSER_AVORA_AVORA_UPDATER_H_

#include <string>

#include "base/observer_list_types.h"

namespace avora {

// Keeps an installed Avora up to date, using Sparkle.  Chromium's own updater
// only exists in Google Chrome builds.
//
// Sparkle.framework and the feed settings are added to the app when it is
// packaged (website/scripts/package-dmg.sh), so a build from out/Default has
// neither and everything here is inert.
//
// A scheduled check never interrupts browsing.  When it finds an update it is
// held as the waiting update, and the Space bar's update button appears; that
// button, or Check for Updates in the app menu, opens Sparkle's window with the
// release notes and the choice to install.
class UpdaterObserver : public base::CheckedObserver {
 public:
  // The waiting update appeared or went away.
  virtual void OnWaitingUpdateChanged() = 0;
};

// True when this copy was packaged with an updater.
bool IsUpdaterEnabled();

// Starts checking on Sparkle's schedule.  Called once, when startup finishes.
void StartUpdater();

// Opens Sparkle's window: the waiting update if there is one, otherwise the
// result of a fresh check.
void CheckForUpdates();

// The version of the waiting update, or empty when there isn't one.
std::u16string WaitingUpdateVersion();

void AddUpdaterObserver(UpdaterObserver* observer);
void RemoveUpdaterObserver(UpdaterObserver* observer);

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_UPDATER_H_
