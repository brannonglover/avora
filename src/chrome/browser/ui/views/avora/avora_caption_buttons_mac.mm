// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/avora/avora_caption_buttons_mac.h"

#import <Cocoa/Cocoa.h>

namespace avora {

void SetCaptionButtonsHidden(gfx::NativeWindow window, bool hidden) {
  NSWindow* const ns_window = window.GetNativeNSWindow();
  if (!ns_window) {
    // Windows hosted out of process (app shims) have no NSWindow here.  Avora's
    // browser windows are in-process, so this is a no-op rather than a failure.
    return;
  }

  static constexpr NSWindowButton kButtons[] = {
      NSWindowCloseButton,
      NSWindowMiniaturizeButton,
      NSWindowZoomButton,
  };
  for (NSWindowButton type : kButtons) {
    if (NSButton* const button = [ns_window standardWindowButton:type]) {
      button.hidden = hidden;
    }
  }
}

}  // namespace avora
