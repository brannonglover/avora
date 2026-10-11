// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_defaults.h"

#include "components/prefs/pref_service.h"

namespace avora {

void ApplyBrowserDefaults(PrefService* pref_service) {
  if (!pref_service) {
    return;
  }

  // Nothing to do yet.  Note for anyone adding to this: writing a pref here
  // cannot change a window's initial vertical-tab-strip width.  This runs
  // from the AvoraSidebarView constructor, which is *after*
  // BrowserWindowFeatures has already read the pref into
  // VerticalTabStripStateController, and that controller writes its own
  // snapshot back over the pref as soon as the window becomes active.  The
  // sidebar's launch width is set by
  // tabs::kVerticalTabStripDefaultUncollapsedWidth in
  // chrome/browser/ui/tabs/vertical_tab_strip_state.h instead.
}

}  // namespace avora
