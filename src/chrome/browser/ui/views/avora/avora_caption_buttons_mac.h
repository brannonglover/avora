// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_CAPTION_BUTTONS_MAC_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_CAPTION_BUTTONS_MAC_H_

#include "ui/gfx/native_ui_types.h"

namespace avora {

// Shows or hides the window's close/minimise/zoom buttons.
//
// Avora seats the traffic lights in the sidebar's nav row, so hiding the side
// nav (CMD+S) would otherwise leave them stranded over the page.  Chromium
// only offers the opposite control -- `setAlwaysShowTrafficLights:`, which
// immersive fullscreen uses to force them on -- so Avora reaches the buttons
// itself.
//
// Callers must restore the buttons when the sidebar comes back; nothing else
// unhides them.
void SetCaptionButtonsHidden(gfx::NativeWindow window, bool hidden);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_CAPTION_BUTTONS_MAC_H_
