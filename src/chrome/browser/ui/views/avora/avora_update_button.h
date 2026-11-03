// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_UPDATE_BUTTON_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_UPDATE_BUTTON_H_

#include "chrome/browser/avora/avora_updater.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/multi_animation.h"
#include "ui/views/controls/button/button.h"

namespace gfx {
class Canvas;
}

namespace avora {

// The notice that an update is ready: an accent glyph in the Space bar, there
// only while an update is waiting.  Pressing it opens Sparkle's update window,
// so nothing happens to the browser until the user chooses to install.
//
// While it's there it pulses every few seconds -- the glyph brightens, a ring
// spreads off the circle and the arrow lifts -- so it's noticed without asking
// for attention constantly.  With Reduce Motion on it holds still instead.
class UpdateButton : public views::Button, public UpdaterObserver {
  METADATA_HEADER(UpdateButton, views::Button)

 public:
  UpdateButton();
  UpdateButton(const UpdateButton&) = delete;
  UpdateButton& operator=(const UpdateButton&) = delete;
  ~UpdateButton() override;

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override;
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

  // UpdaterObserver:
  void OnWaitingUpdateChanged() override;

 private:
  // 0 to 1 over each pulse, then held at 1 between pulses.
  gfx::MultiAnimation pulse_;
  bool painted_rest_ = false;
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_UPDATE_BUTTON_H_
