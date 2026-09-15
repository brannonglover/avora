// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_space_gesture_controller.h"

#include <cmath>

#include "base/i18n/rtl.h"
#include "ui/events/event.h"
#include "ui/views/view.h"

namespace avora {

AvoraSpaceGestureController::AvoraSpaceGestureController(
    WindowSpaceState* window_space_state,
    std::vector<views::View*> target_views)
    : window_space_state_(window_space_state) {
  for (views::View* view : target_views) {
    AttachToView(view);
  }
}

AvoraSpaceGestureController::~AvoraSpaceGestureController() {
  for (views::View* view : attached_views_) {
    DetachFromView(view);
  }
}

void AvoraSpaceGestureController::ActivateAdjacentSpace(bool forward) {
  if (window_space_state_) {
    window_space_state_->ActivateAdjacentSpace(forward);
  }
}

void AvoraSpaceGestureController::AttachToView(views::View* view) {
  if (!view) {
    return;
  }
  view->AddPreTargetHandler(this);
  attached_views_.push_back(view);
}

void AvoraSpaceGestureController::DetachFromView(views::View* view) {
  if (!view) {
    return;
  }
  view->RemovePreTargetHandler(this);
}

#if BUILDFLAG(IS_MAC)
void AvoraSpaceGestureController::OnScrollEvent(ui::ScrollEvent* event) {
  if (event->type() == ui::EventType::kScrollFlingStart ||
      event->type() == ui::EventType::kScrollFlingCancel) {
    if (gesture_state_ == GestureState::kHorizontalSwipe) {
      event->SetHandled();
      event->StopPropagation();
    }
    ResetGesture();
    return;
  }

  if (event->momentum_phase() != ui::EventMomentumPhase::NONE) {
    if (gesture_state_ == GestureState::kHorizontalSwipe) {
      event->SetHandled();
      event->StopPropagation();
    }
    if (event->momentum_phase() == ui::EventMomentumPhase::END) {
      ResetGesture();
    }
    return;
  }

  if (event->scroll_event_phase() == ui::ScrollEventPhase::kBegan) {
    ResetGesture();
  }

  if (event->scroll_event_phase() == ui::ScrollEventPhase::kEnd) {
    if (gesture_state_ == GestureState::kHorizontalSwipe) {
      event->SetHandled();
      event->StopPropagation();
    }
    ResetGesture();
    return;
  }

  ProcessSwipeOffset(event->x_offset(), event->y_offset(), event);
}
#endif

void AvoraSpaceGestureController::ProcessSwipeOffset(float x_offset,
                                                     float y_offset,
                                                     ui::Event* event) {
  base::TimeTicks now = base::TimeTicks::Now();
  if (!last_event_time_.is_null() &&
      (now - last_event_time_) > kGestureResetTimeout) {
    ResetGesture();
  }
  last_event_time_ = now;

  if (gesture_state_ == GestureState::kVerticalPassthrough) {
    return;
  }

  cumulative_x_ += x_offset;
  cumulative_y_ += y_offset;

  if (gesture_state_ == GestureState::kNone) {
    if (std::abs(cumulative_y_) >= kAxisLockThreshold &&
        std::abs(cumulative_y_) > std::abs(cumulative_x_)) {
      gesture_state_ = GestureState::kVerticalPassthrough;
      return;
    }

    if (std::abs(cumulative_x_) >= kAxisLockThreshold &&
        std::abs(cumulative_x_) > std::abs(cumulative_y_)) {
      gesture_state_ = GestureState::kHorizontalSwipe;
    }
  }

  if (gesture_state_ == GestureState::kHorizontalSwipe) {
    event->SetHandled();
    event->StopPropagation();

    if (!has_triggered_in_current_gesture_ &&
        std::abs(cumulative_x_) >= kSwipeThreshold) {
      bool forward = cumulative_x_ < 0;
      if (base::i18n::IsRTL()) {
        forward = !forward;
      }
      has_triggered_in_current_gesture_ = true;
      ActivateAdjacentSpace(forward);
    }
  }
}

void AvoraSpaceGestureController::ResetGesture() {
  gesture_state_ = GestureState::kNone;
  cumulative_x_ = 0.0f;
  cumulative_y_ = 0.0f;
  has_triggered_in_current_gesture_ = false;
}

}  // namespace avora
