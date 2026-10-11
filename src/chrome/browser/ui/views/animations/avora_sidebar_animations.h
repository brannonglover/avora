// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_ANIMATIONS_AVORA_SIDEBAR_ANIMATIONS_H_
#define CHROME_BROWSER_UI_VIEWS_ANIMATIONS_AVORA_SIDEBAR_ANIMATIONS_H_

#include "chrome/browser/ui/animation/browser_animation_provider.h"
#include "chrome/browser/ui/animation/browser_animation_types.h"
#include "ui/base/identifier/unique_identifier.h"
#include "ui/base/interaction/safe_castable.h"

// Provides the Avora side nav show/hide animation (Cmd-S).
//
// This is distinct from the vertical tab strip's own collapse animation: that
// one narrows the strip to a rail of favicons, while this one slides the whole
// sidebar -- Avora header, tab strip and spaces bar alike -- out past the
// leading edge of the window and lets the page take over the space.
class AvoraSidebarAnimations : public CachingBrowserAnimationProvider {
 public:
  // Required to support retrieval.
  DECLARE_SAFE_CAST_TARGET()

  AvoraSidebarAnimations();
  ~AvoraSidebarAnimations() override;

  DECLARE_CLASS_BROWSER_ANIMATION_GROUP(kSidebar);

  DECLARE_CLASS_BROWSER_ANIMATION_MOTION(kHide);
  DECLARE_CLASS_BROWSER_ANIMATION_MOTION(kShow);

  // Hovering the window's leading edge peeks the hidden sidebar back in over
  // the page.  Deliberately separate from kHide/kShow: revealing is a
  // temporary look at the sidebar, not a change to whether it is hidden.
  DECLARE_CLASS_BROWSER_ANIMATION_MOTION(kReveal);
  DECLARE_CLASS_BROWSER_ANIMATION_MOTION(kConceal);

  // How far the sidebar has slid out of the window, where 0.0 is fully
  // on-screen and 1.0 is fully off-screen. The layout multiplies the sidebar's
  // width by this to get the slide distance, so the same value drives both the
  // sidebar's position and how much room the contents gain.
  DECLARE_CLASS_BROWSER_ANIMATION_SEQUENCE(kHiddenFraction);

  // How far the hidden sidebar has been peeked back in, 0.0 fully out to 1.0
  // fully on screen.  This moves the sidebar only -- the page keeps the width
  // it gained, so the sidebar floats over it rather than pushing it aside.
  DECLARE_CLASS_BROWSER_ANIMATION_SEQUENCE(kOverlayFraction);

  // CachingBrowserAnimationProvider:
  GroupInfos GenerateAnimations() const override;
};

#endif  // CHROME_BROWSER_UI_VIEWS_ANIMATIONS_AVORA_SIDEBAR_ANIMATIONS_H_
