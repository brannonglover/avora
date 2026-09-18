// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_SESSIONS_AVORA_CRASH_RESTORE_H_
#define CHROME_BROWSER_SESSIONS_AVORA_CRASH_RESTORE_H_

class BrowserWindowInterface;

namespace avora {

// Restores the previous session into |browser| after an unclean exit, without
// asking first.
//
// Upstream never restores automatically after a crash -- it opens a New Tab
// Page and offers the restore through SessionCrashedBubble -- because a
// session that is itself the cause of the crash would otherwise be reloaded on
// every launch, wedging the browser in a crash loop.  Avora's daily tabs are
// the user's working state rather than a browsing history, so losing them to a
// crash is not an acceptable default; instead of declining to restore, we
// restore and break the loop on the second attempt.
//
// Only to be called when HasPendingUncleanExit() is true, i.e. exactly where
// the bubble would otherwise be shown.  Returns false, leaving the bubble as
// the fallback, when the crash-loop guard declines: a crash that arrives
// within avora::kCrashRestoreMinUptime of the last automatic restore is taken
// as evidence that the restored session is what crashed.
bool RestorePreviousSessionAfterCrash(BrowserWindowInterface* browser);

}  // namespace avora

#endif  // CHROME_BROWSER_SESSIONS_AVORA_CRASH_RESTORE_H_
