// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/animations/avora_sidebar_animations.h"

#include "chrome/browser/ui/animation/browser_animation_types.h"
#include "ui/base/interaction/safe_castable.h"
#include "ui/gfx/animation/tween.h"

DEFINE_SAFE_CAST_TARGET(AvoraSidebarAnimations)

DEFINE_CLASS_BROWSER_ANIMATION_GROUP(AvoraSidebarAnimations, kSidebar);

DEFINE_CLASS_BROWSER_ANIMATION_MOTION(AvoraSidebarAnimations, kHide);
DEFINE_CLASS_BROWSER_ANIMATION_MOTION(AvoraSidebarAnimations, kShow);
DEFINE_CLASS_BROWSER_ANIMATION_MOTION(AvoraSidebarAnimations, kReveal);
DEFINE_CLASS_BROWSER_ANIMATION_MOTION(AvoraSidebarAnimations, kConceal);

DEFINE_CLASS_BROWSER_ANIMATION_SEQUENCE(AvoraSidebarAnimations,
                                        kHiddenFraction);
DEFINE_CLASS_BROWSER_ANIMATION_SEQUENCE(AvoraSidebarAnimations,
                                        kOverlayFraction);

AvoraSidebarAnimations::AvoraSidebarAnimations() {
  // Showing is the resting state, and the value has to persist between motions
  // so that a toggle part way through the previous one picks up where it left
  // off rather than snapping to an end and animating back.
  SetSequenceParams(kSidebar,
                    Default(kHiddenFraction, 0.0, /*auto_return=*/false),
                    Default(kOverlayFraction, 0.0, /*auto_return=*/false));
}

AvoraSidebarAnimations::~AvoraSidebarAnimations() = default;

AvoraSidebarAnimations::GroupInfos AvoraSidebarAnimations::GenerateAnimations()
    const {
  // Hiding is a touch quicker than showing: getting out of the way should feel
  // immediate, while the sidebar coming back reads better if it settles.
  constexpr int kHideMs = 220;
  constexpr int kShowMs = 260;
  // The peek is a response to the pointer, so it wants to feel more immediate
  // than the deliberate CMD+S toggle.
  constexpr int kRevealMs = 180;
  constexpr int kConcealMs = 160;
  constexpr auto kTween = gfx::Tween::EASE_IN_OUT_EMPHASIZED;

  return Groups(Group(
      kSidebar,
      Motion(kHide, TotalDurationMs(kHideMs), kTween,
             Animate(kHiddenFraction, FromValue(0.0), ToValue(1.0))),
      Motion(kShow, TotalDurationMs(kShowMs), kTween,
             Animate(kHiddenFraction, FromValue(1.0), ToValue(0.0))),
      Motion(kReveal, TotalDurationMs(kRevealMs), kTween,
             Animate(kOverlayFraction, FromValue(0.0), ToValue(1.0))),
      Motion(kConceal, TotalDurationMs(kConcealMs), kTween,
             Animate(kOverlayFraction, FromValue(1.0), ToValue(0.0)))));
}
