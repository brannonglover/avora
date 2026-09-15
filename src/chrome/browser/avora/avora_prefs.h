// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_PREFS_H_
#define CHROME_BROWSER_AVORA_AVORA_PREFS_H_

#include "base/time/time.h"

class PrefRegistrySimple;
class PrefService;

namespace avora {

// How long a "today" tab survives without being visited before it is swept.
// Zero disables expiry entirely.
inline constexpr char kTodayTabExpiryHoursPref[] =
    "avora.today_tab_expiry_hours";
inline constexpr int kDefaultTodayTabExpiryHours = 12;

// Whether the first-run import offer has been shown.  Set to true after
// the offer is displayed (whether accepted or dismissed) so it is not
// shown again.
inline constexpr char kImportOfferedPref[] = "avora.import_offered";

// Registers every Avora profile pref.
//
// This is the single entry point Chromium's browser_prefs.cc calls, so adding
// a new Avora pref only ever means editing Avora-owned code.  Keep all new
// registrations here rather than in browser_prefs.cc.
void RegisterProfilePrefs(PrefRegistrySimple* registry);

// Configured today-tab lifetime.  A zero delta means "never expire".
base::TimeDelta GetTodayTabExpiry(PrefService* pref_service);

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_PREFS_H_
