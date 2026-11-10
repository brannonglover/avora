// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PINNED_URL_DIALOG_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PINNED_URL_DIALOG_H_

#include <optional>
#include <string>

#include "base/functional/callback_forward.h"
#include "ui/gfx/native_ui_types.h"

namespace views {
class Widget;
}

namespace avora {

// The Widget::InitParams::name the dialog's widget carries, so tests can
// wait for it with views::NamedWidgetShownWaiter.
inline constexpr char kPinnedUrlDialogName[] = "AvoraPinnedUrlDialog";

// Returns nullopt when |text| may be saved as the pin's URL.  Otherwise Save
// is disabled and the returned message (which may be empty, for a field that
// is merely blank) is shown under the field.  Asked on every keystroke.
using PinnedUrlValidator =
    base::RepeatingCallback<std::optional<std::u16string>(
        const std::u16string& text)>;

// Runs with the field's text when Save is pressed.  Not run on Cancel.
using PinnedUrlSaveCallback = base::OnceCallback<void(const std::u16string&)>;

// The "Edit URL" dialog for a pinned item: one field, prefilled with
// |current_url| and selected, so typing replaces it and Return saves.
// Window-modal to |parent|.  Returns the dialog's widget.
views::Widget* ShowPinnedUrlDialog(gfx::NativeWindow parent,
                                   const std::u16string& current_url,
                                   PinnedUrlValidator validate,
                                   PinnedUrlSaveCallback on_save);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PINNED_URL_DIALOG_H_
