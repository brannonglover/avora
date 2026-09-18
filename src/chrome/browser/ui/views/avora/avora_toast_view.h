// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_TOAST_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_TOAST_VIEW_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/views/view.h"

namespace gfx {
struct VectorIcon;
}

namespace views {
class ImageView;
class Label;
}  // namespace views

namespace avora {

// Transient confirmation popup ("URL copied") shown in the upper-right corner
// of the content area.  It fades and slides in, holds for a beat, then fades
// out on its own; it never takes focus and never swallows events, so the page
// underneath keeps behaving as if the toast were not there.
//
// One instance per window, created hidden and reused for every message.
class AvoraToastView : public views::View,
                       public ui::ImplicitAnimationObserver {
  METADATA_HEADER(AvoraToastView, views::View)

 public:
  // Space the view reserves around the card for its drop shadow.  The view
  // paints to a layer, which clips at its bounds, so this must cover the
  // widest shadow (offset + blur) or the shadow is cut off.  The layout
  // positions the view itself, so a caller wanting an N px visual gap from the
  // content edge should inset by N - kShadowMargin.
  static constexpr int kShadowMargin = 28;

  AvoraToastView();
  AvoraToastView(const AvoraToastView&) = delete;
  AvoraToastView& operator=(const AvoraToastView&) = delete;
  ~AvoraToastView() override;

  // Shows |message| next to |icon|, restarting the dismiss countdown if the
  // toast is already on screen (so repeated presses read as one toast).
  void Show(const std::u16string& message, const gfx::VectorIcon& icon);

  // Fades the toast out early.
  void Hide();

  // ui::ImplicitAnimationObserver:
  void OnImplicitAnimationsCompleted() override;

 private:
  void StartFadeOut();

  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;

  // Set while the fade-out animation runs, so its completion hides the view
  // but the fade-in's completion does not.
  bool fading_out_ = false;

  base::OneShotTimer dismiss_timer_;
  base::WeakPtrFactory<AvoraToastView> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_TOAST_VIEW_H_
