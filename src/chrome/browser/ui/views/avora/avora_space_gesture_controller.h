// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SPACE_GESTURE_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SPACE_GESTURE_CONTROLLER_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "ui/events/event_handler.h"

namespace views {
class View;
}  // namespace views

namespace avora {

// Handles horizontal trackpad swipes over the Avora sidebar region to switch
// Spaces.  Installed as a pre-target handler on each view in the region.
class AvoraSpaceGestureController : public ui::EventHandler {
 public:
  static constexpr float kAxisLockThreshold = 15.0f;
  static constexpr float kSwipeThreshold = 75.0f;
  static constexpr base::TimeDelta kGestureResetTimeout =
      base::Milliseconds(300);

  AvoraSpaceGestureController(WindowSpaceState* window_space_state,
                              std::vector<views::View*> target_views);
  AvoraSpaceGestureController(const AvoraSpaceGestureController&) = delete;
  AvoraSpaceGestureController& operator=(const AvoraSpaceGestureController&) =
      delete;
  ~AvoraSpaceGestureController() override;

  void ActivateAdjacentSpace(bool forward);

#if BUILDFLAG(IS_MAC)
  void OnScrollEvent(ui::ScrollEvent* event) override;
#endif

 private:
  enum class GestureState {
    kNone,
    kHorizontalSwipe,
    kVerticalPassthrough,
  };

  void AttachToView(views::View* view);
  void DetachFromView(views::View* view);
  void ProcessSwipeOffset(float x_offset, float y_offset, ui::Event* event);
  void ResetGesture();

  raw_ptr<WindowSpaceState> window_space_state_ = nullptr;
  std::vector<raw_ptr<views::View>> attached_views_;

  GestureState gesture_state_ = GestureState::kNone;
  float cumulative_x_ = 0.0f;
  float cumulative_y_ = 0.0f;
  bool has_triggered_in_current_gesture_ = false;
  base::TimeTicks last_event_time_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SPACE_GESTURE_CONTROLLER_H_
