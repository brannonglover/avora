// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_style_ai_key_dialog.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace avora {

std::unique_ptr<views::DialogDelegate> MakeStyleAiKeyDialog(
    bool replacing,
    StyleAiKeySubmitCallback on_submit) {
  auto delegate = std::make_unique<views::DialogDelegate>();
  delegate->SetModalType(ui::mojom::ModalType::kWindow);
  delegate->SetTitle(replacing ? u"Replace Anthropic API key"
                               : u"Add Anthropic API key");
  delegate->SetButtonLabel(ui::mojom::DialogButton::kOk, u"Save");
  delegate->SetShowCloseButton(true);

  auto contents = std::make_unique<views::View>();
  contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(16), 10));

  auto* description = contents->AddChildView(std::make_unique<views::Label>(
      u"Style Studio sends the element you describe and its styles to "
      u"Anthropic with this key, only when you press Generate. Avora stores "
      u"the key encrypted on this device and never shows it again."));
  description->SetMultiLine(true);
  description->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  description->SetMaximumWidth(380);

  auto* key_field =
      contents->AddChildView(std::make_unique<views::Textfield>());
  key_field->SetTextInputType(ui::TEXT_INPUT_TYPE_PASSWORD);
  key_field->SetPlaceholderText(u"sk-ant-...");
  key_field->SetAccessibleName(u"Anthropic API key");

  delegate->SetContentsView(std::move(contents));
  delegate->SetInitiallyFocusedView(key_field);

  // The contents view outlives the accept callback, so the raw field pointer
  // is safe to read here.
  delegate->SetAcceptCallback(base::BindOnce(
      [](views::Textfield* field, StyleAiKeySubmitCallback on_submit) {
        std::string key = base::UTF16ToUTF8(field->GetText());
        field->SetText(std::u16string());
        std::move(on_submit).Run(std::move(key));
      },
      key_field, std::move(on_submit)));
  return delegate;
}

void ShowStyleAiKeyDialog(content::WebContents* web_contents,
                          bool replacing,
                          StyleAiKeySubmitCallback on_submit) {
  if (!web_contents) {
    return;
  }
  views::DialogDelegate::CreateDialogWidget(
      MakeStyleAiKeyDialog(replacing, std::move(on_submit)),
      gfx::NativeWindow(), web_contents->GetNativeView())
      ->Show();
}

}  // namespace avora
