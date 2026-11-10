// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_pinned_url_dialog.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace avora {

namespace {

constexpr int kFieldWidth = 360;

// The dialog's contents: the URL field and, under it, why Save is off when
// it is.  Owns the dialog's delegate, as AvoraImportDialogView does: the
// widget needs the delegate for as long as it lives and tears this view down
// last, so the delegate's callbacks can safely point back here.
class PinnedUrlDialogView : public views::View,
                            public views::TextfieldController {
  METADATA_HEADER(PinnedUrlDialogView, views::View)

 public:
  PinnedUrlDialogView(std::unique_ptr<views::DialogDelegate> delegate,
                      const std::u16string& current_url,
                      PinnedUrlValidator validate,
                      PinnedUrlSaveCallback on_save)
      : delegate_(std::move(delegate)),
        validate_(std::move(validate)),
        on_save_(std::move(on_save)) {
    delegate_->SetTitle(u"Edit URL");
    delegate_->SetModalType(ui::mojom::ModalType::kWindow);
    delegate_->SetButtonLabel(ui::mojom::DialogButton::kOk, u"Save");
    delegate_->SetShowCloseButton(false);
    delegate_->set_internal_name(kPinnedUrlDialogName);
    delegate_->SetAcceptCallback(base::BindOnce(
        &PinnedUrlDialogView::Save, base::Unretained(this)));

    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(12, 0), 6));

    field_ = AddChildView(std::make_unique<views::Textfield>());
    field_->SetText(current_url);
    field_->SetAccessibleName(u"URL");
    field_->SetDefaultWidthInChars(1);
    field_->SetPreferredSize(
        gfx::Size(kFieldWidth, field_->GetPreferredSize().height()));
    field_->set_controller(this);

    error_ = AddChildView(std::make_unique<views::Label>(
        std::u16string(), views::style::CONTEXT_DIALOG_BODY_TEXT,
        views::style::STYLE_SECONDARY));
    error_->SetHorizontalAlignment(gfx::ALIGN_LEFT);

    delegate_->SetInitiallyFocusedView(field_);
    Validate();
  }
  PinnedUrlDialogView(const PinnedUrlDialogView&) = delete;
  PinnedUrlDialogView& operator=(const PinnedUrlDialogView&) = delete;
  ~PinnedUrlDialogView() override { field_->set_controller(nullptr); }

  views::DialogDelegate* delegate() { return delegate_.get(); }

  // views::View:
  void AddedToWidget() override {
    // Selected, so the first keystroke replaces the old URL rather than
    // appending to it.  Not before: a field outside a widget has no
    // selection to keep.
    field_->SelectAll(/*reversed=*/false);
  }

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override {
    Validate();
  }

 private:
  void Validate() {
    const std::optional<std::u16string> problem =
        validate_ ? validate_.Run(std::u16string(field_->GetText()))
                  : std::nullopt;
    // The label keeps its place in the layout while empty, so the dialog
    // does not change height as the user types.
    error_->SetText(problem.value_or(std::u16string()));
    delegate_->SetButtonEnabled(ui::mojom::DialogButton::kOk,
                                !problem.has_value());
  }

  void Save() {
    if (on_save_) {
      std::move(on_save_).Run(std::u16string(field_->GetText()));
    }
  }

  std::unique_ptr<views::DialogDelegate> delegate_;
  PinnedUrlValidator validate_;
  PinnedUrlSaveCallback on_save_;
  raw_ptr<views::Textfield> field_ = nullptr;
  raw_ptr<views::Label> error_ = nullptr;
};

BEGIN_METADATA(PinnedUrlDialogView)
END_METADATA

}  // namespace

views::Widget* ShowPinnedUrlDialog(gfx::NativeWindow parent,
                                   const std::u16string& current_url,
                                   PinnedUrlValidator validate,
                                   PinnedUrlSaveCallback on_save) {
  auto view = std::make_unique<PinnedUrlDialogView>(
      std::make_unique<views::DialogDelegate>(), current_url,
      std::move(validate), std::move(on_save));
  views::DialogDelegate* delegate = view->delegate();
  delegate->SetContentsView(std::move(view));
  views::Widget* widget =
      views::DialogDelegate::CreateDialogWidget(delegate, parent,
                                                gfx::NativeView());
  widget->Show();
  return widget;
}

}  // namespace avora
