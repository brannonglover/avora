// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SPACE_ICON_PICKER_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SPACE_ICON_PICKER_H_

#include <string>

#include "base/functional/callback_forward.h"

namespace views {
class View;
}

namespace avora {

// Everything the Space editor can change.
struct SpaceEditorFields {
  std::string name;

  // Lucide icon identifier, e.g. "briefcase".
  std::string icon;

  // Accent colour as "#RRGGBB"; also the colour the icon is drawn in.
  std::string accent_color;
};

// Shows the Space editor -- name, accent colour, and the curated Lucide icon
// picker -- in a bubble anchored to |anchor_view|.  |on_accept| runs with the
// chosen values when the user confirms, and not at all when they cancel.
//
// The same bubble backs both flows: creating a Space ("New Space" / "Create")
// and editing one ("Edit Space" / "Save").
void ShowSpaceEditorBubble(
    views::View* anchor_view,
    const std::u16string& title,
    const std::u16string& confirm_label,
    const SpaceEditorFields& initial_fields,
    base::OnceCallback<void(const SpaceEditorFields&)> on_accept);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SPACE_ICON_PICKER_H_
