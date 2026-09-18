// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/sessions/avora_crash_restore.h"

#include <memory>

#include "base/time/time.h"
#include "chrome/browser/avora/avora_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/exit_type_service.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/prefs/pref_service.h"

namespace avora {

bool RestorePreviousSessionAfterCrash(BrowserWindowInterface* browser) {
  Profile* profile = browser ? browser->GetProfile() : nullptr;
  if (!profile || profile->IsOffTheRecord()) {
    return false;
  }

  PrefService* prefs = profile->GetPrefs();
  const base::Time now = base::Time::Now();
  const base::Time last_restore = prefs->GetTime(kLastCrashRestorePref);

  // Crash-loop guard.  |last_restore| is stamped just before each automatic
  // restore, so now - last_restore is how long the browser survived the last
  // one: a short-lived session is one the restore itself brought down, and
  // restoring it again would only repeat the crash.  A clock that has moved
  // backwards since the stamp yields no usable interval, so it is treated as
  // no evidence of a loop rather than as a reason to decline.
  if (!last_restore.is_null() && now >= last_restore &&
      now - last_restore < kCrashRestoreMinUptime) {
    return false;
  }
  prefs->SetTime(kLastCrashRestorePref, now);

  // Mirrors SessionCrashedBubbleDelegate::RestorePreviousSession(): the lock
  // has to outlive the RestoreSessionAfterCrash() call, or ExitTypeService
  // sees the last lock go away before the restore has started and clears the
  // crashed state early -- which would lose the session for good if we crashed
  // again mid-restore.  Holding it until this function returns lets
  // ExitTypeService wait for the restore to complete instead.
  std::unique_ptr<ExitTypeService::CrashedLock> crashed_lock;
  if (ExitTypeService* exit_type_service =
          ExitTypeService::GetInstanceForProfile(profile)) {
    crashed_lock = exit_type_service->CreateCrashedLock();
  }

  SessionRestore::RestoreSessionAfterCrash(browser);
  return true;
}

}  // namespace avora
