// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_defaults.h"

#include "components/prefs/pref_service.h"

namespace avora {

namespace {

// Must match the string in chrome/common/pref_names.h
// (prefs::kVerticalTabsUncollapsedWidth).
constexpr char kVerticalTabsUncollapsedWidth[] =
    "vertical_tab_strip_uncollapsed_width";

}  // namespace

void ApplyBrowserDefaults(PrefService* prefs) {
  if (!prefs) {
    return;
  }

  // Only override the sidebar width when it is still at Chromium's stock
  // default (240 px).  This is a one-time migration: once the value is
  // changed (either here or by the user dragging the resize handle) it
  // will no longer match the stock default and will be left alone.
  const PrefService::Preference* pref =
      prefs->FindPreference(kVerticalTabsUncollapsedWidth);
  if (pref && prefs->GetInteger(kVerticalTabsUncollapsedWidth) ==
                  kChromiumDefaultSidebarWidth) {
    prefs->SetInteger(kVerticalTabsUncollapsedWidth,
                      kAvoraDefaultSidebarWidth);
  }
}

}  // namespace avora
