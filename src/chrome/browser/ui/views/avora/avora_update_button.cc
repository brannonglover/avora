// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_update_button.h"

#include <string>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/views/avora/avora_lucide_icon.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace avora {

namespace {

// Matched to the Space bar's own metrics, like the downloads button.
constexpr int kButtonSize = 28;
constexpr int kGlyphSize = 16;
constexpr int kCornerRadius = 8;

constexpr SkColor kAccent = SkColorSetRGB(0x6E, 0xA1, 0xF7);
constexpr SkColor kRestBg = SkColorSetA(kAccent, 0x26);
constexpr SkColor kHoverBg = SkColorSetA(kAccent, 0x40);

// Lucide "circle-arrow-up".
constexpr char kUpdateIconPath[] =
    "M22 12a10 10 0 1 1-20 0 10 10 0 0 1 20 0z"
    "M16 12l-4-4-4 4"
    "M12 16V8";

}  // namespace

UpdateButton::UpdateButton()
    : views::Button(base::BindRepeating(&CheckForUpdates)) {
  SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
  AddUpdaterObserver(this);
  OnWaitingUpdateChanged();
}

UpdateButton::~UpdateButton() {
  RemoveUpdaterObserver(this);
}

void UpdateButton::PaintButtonContents(gfx::Canvas* canvas) {
  const bool hovered = GetState() == views::Button::STATE_HOVERED ||
                       GetState() == views::Button::STATE_PRESSED;
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(hovered ? kHoverBg : kRestBg);
  canvas->DrawRoundRect(GetLocalBounds(), kCornerRadius, flags);

  gfx::Rect glyph(GetLocalBounds());
  glyph.ClampToCenteredSize(gfx::Size(kGlyphSize, kGlyphSize));
  PaintLucidePathData(canvas, glyph, kUpdateIconPath, kAccent);
}

void UpdateButton::OnWaitingUpdateChanged() {
  const std::u16string version = WaitingUpdateVersion();
  SetVisible(!version.empty());
  if (version.empty()) {
    return;
  }
  const std::u16string label = base::StrCat({u"Update to Avora ", version});
  SetTooltipText(label);
  GetViewAccessibility().SetName(label);
}

BEGIN_METADATA(UpdateButton)
END_METADATA

}  // namespace avora
