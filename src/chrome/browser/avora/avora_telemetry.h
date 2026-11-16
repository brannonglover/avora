// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_TELEMETRY_H_
#define CHROME_BROWSER_AVORA_AVORA_TELEMETRY_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

class PrefRegistrySimple;
class PrefService;

namespace avora {

// Usage statistics: once a day, while Avora is open, it sends one "app_active"
// event to PostHog so we can tell how many copies are in use, and for how long
// people keep using it.  The event carries a random install ID, the app,
// Chromium and macOS versions, the chip, the UI language and rough counts of
// windows, tabs and Spaces.  Never a page address, title, search or anything
// typed.
//
// Only packaged copies send (the same test as the updater), so builds from
// out/Default never do, unless --avora-telemetry-url sends a test copy's events
// to a local server instead.  Turning it off in chrome://avora-settings also
// forgets the install ID, so turning it back on starts as a new install.

inline constexpr char kTelemetryEnabledPref[] = "avora.telemetry.enabled";
inline constexpr char kTelemetryInstallIdPref[] = "avora.telemetry.install_id";
inline constexpr char kTelemetryLastActiveDayPref[] =
    "avora.telemetry.last_active_day";

// Sends a test copy's events to this URL instead of PostHog, and lets an
// unpackaged build send at all.
inline constexpr char kTelemetryUrlSwitch[] = "avora-telemetry-url";

// Counts taken when the event is sent.
struct TelemetryCounts {
  int windows = 0;
  int tabs = 0;
  int spaces = 0;
};

struct TelemetryConfig {
  // Local State.  Must outlive the browser's main loop, as Local State does.
  raw_ptr<PrefService> local_state;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory;
  base::RepeatingCallback<TelemetryCounts()> counts;
  // Avora's own version (CFBundleVersion), e.g. "1.5.0".
  std::string app_version;
  std::string locale;
  // Whether this copy was packaged with an updater.
  bool packaged = false;
};

void RegisterTelemetryLocalStatePrefs(PrefRegistrySimple* registry);

// Starts the daily check.  Called once, when startup finishes.
void StartTelemetry(TelemetryConfig config);

// For chrome://avora-settings.  Both are false/no-ops before StartTelemetry.
bool IsTelemetryEnabled();
void SetTelemetryEnabled(bool enabled);

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_TELEMETRY_H_
