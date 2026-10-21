// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_STYLE_AI_KEY_DIALOG_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_STYLE_AI_KEY_DIALOG_H_

#include <memory>
#include <string>

#include "base/functional/callback_forward.h"

namespace content {
class WebContents;
}

namespace views {
class DialogDelegate;
}

namespace avora {

// Called with the entered key when the user confirms the dialog. Not called if
// the dialog is cancelled or closed.
using StyleAiKeySubmitCallback = base::OnceCallback<void(std::string key)>;

// Builds the native dialog that collects the Anthropic API key. The key is
// typed into a password field in the browser process and handed straight to
// `on_submit`; it never passes through a renderer, so the Settings page that
// opens the dialog never sees it.
std::unique_ptr<views::DialogDelegate> MakeStyleAiKeyDialog(
    bool replacing,
    StyleAiKeySubmitCallback on_submit);

// Shows MakeStyleAiKeyDialog() modal to the window that hosts `web_contents`.
void ShowStyleAiKeyDialog(content::WebContents* web_contents,
                          bool replacing,
                          StyleAiKeySubmitCallback on_submit);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_STYLE_AI_KEY_DIALOG_H_
