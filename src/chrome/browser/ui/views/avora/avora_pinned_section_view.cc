// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_pinned_section_view.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/avora/avora_tab_space.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_materializer.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_tab_marker.h"
#include "chrome/browser/ui/tabs/tab_change_type.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_callback.h"
#include "components/favicon_base/favicon_types.h"
#include "cc/paint/paint_flags.h"
#include "content/public/browser/web_contents.h"
#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_variant.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/border.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace avora {

namespace {

constexpr int kContextMenuAddFolderId = 1;
constexpr int kContextMenuRenameFolderId = 2;
constexpr int kContextMenuDeleteFolderId = 3;
constexpr int kContextMenuRemoveFromFolderId = 4;
constexpr int kContextMenuUnpinTabId = 5;

constexpr SkColor kFolderIconColor = SkColorSetRGB(0xF5, 0xBD, 0x4F);
constexpr SkColor kRowHoverBg = SkColorSetARGB(0x1A, 0xFF, 0xFF, 0xFF);
constexpr SkColor kLabelColor = SkColorSetARGB(0xE0, 0xED, 0xF2, 0xF5);

// Paints a simple folder icon at the given position using Skia.
void PaintFolderIcon(gfx::Canvas* canvas, int x, int y, int size,
                     SkColor color) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(color);
  flags.setStyle(cc::PaintFlags::kFill_Style);

  const float s = static_cast<float>(size);
  const float fx = static_cast<float>(x);
  const float fy = static_cast<float>(y);
  const float r = s * 0.1f;

  // Folder body: rounded rect covering most of the icon area.
  SkRect body = SkRect::MakeXYWH(fx, fy + s * 0.25f, s, s * 0.7f);
  canvas->DrawRoundRect(gfx::SkRectToRectF(body), r, flags);

  // Folder tab: small rounded rect at top-left.
  SkRect tab = SkRect::MakeXYWH(fx, fy + s * 0.12f, s * 0.4f, s * 0.22f);
  canvas->DrawRoundRect(gfx::SkRectToRectF(tab), r, flags);
}

// Context menu delegate for the section-level "Add Folder" action.
class SectionContextMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  explicit SectionContextMenuDelegate(base::OnceClosure on_add_folder)
      : on_add_folder_(std::move(on_add_folder)) {}

  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == kContextMenuAddFolderId && on_add_folder_) {
      std::move(on_add_folder_).Run();
    }
  }

 private:
  base::OnceClosure on_add_folder_;
};

// Context menu delegate for folder header actions.
class FolderContextMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  FolderContextMenuDelegate(base::OnceClosure on_rename,
                            base::OnceClosure on_delete,
                            base::OnceClosure on_add_folder)
      : on_rename_(std::move(on_rename)),
        on_delete_(std::move(on_delete)),
        on_add_folder_(std::move(on_add_folder)) {}

  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == kContextMenuRenameFolderId && on_rename_) {
      std::move(on_rename_).Run();
    } else if (command_id == kContextMenuDeleteFolderId && on_delete_) {
      std::move(on_delete_).Run();
    } else if (command_id == kContextMenuAddFolderId && on_add_folder_) {
      std::move(on_add_folder_).Run();
    }
  }

 private:
  base::OnceClosure on_rename_;
  base::OnceClosure on_delete_;
  base::OnceClosure on_add_folder_;
};

// Context menu delegate for a tab row within a folder.
class TabRowContextMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  explicit TabRowContextMenuDelegate(base::OnceClosure on_remove)
      : on_remove_(std::move(on_remove)) {}

  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == kContextMenuRemoveFromFolderId && on_remove_) {
      std::move(on_remove_).Run();
    }
  }

 private:
  base::OnceClosure on_remove_;
};

// Context menu delegate for standalone pinned tab "Unpin Tab" + "New Folder".
class PinnedTabContextMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  PinnedTabContextMenuDelegate(base::OnceClosure on_unpin,
                               base::OnceClosure on_add_folder)
      : on_unpin_(std::move(on_unpin)),
        on_add_folder_(std::move(on_add_folder)) {}

  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == kContextMenuUnpinTabId && on_unpin_) {
      std::move(on_unpin_).Run();
    } else if (command_id == kContextMenuAddFolderId && on_add_folder_) {
      std::move(on_add_folder_).Run();
    }
  }

 private:
  base::OnceClosure on_unpin_;
  base::OnceClosure on_add_folder_;
};

// A Textfield subclass that calls a callback when it loses focus.
class RenameTextfield : public views::Textfield {
 public:
  explicit RenameTextfield(base::RepeatingClosure on_blur)
      : on_blur_(std::move(on_blur)) {}

  void ClearOnBlur() { on_blur_.Reset(); }

  void OnBlur() override {
    views::Textfield::OnBlur();
    if (on_blur_) {
      on_blur_.Run();
    }
  }

 private:
  base::RepeatingClosure on_blur_;
};

}  // namespace

// ---------------------------------------------------------------------------
// FolderHeaderView
// ---------------------------------------------------------------------------

FolderHeaderView::FolderHeaderView(
    int folder_index,
    const std::string& name,
    bool expanded,
    base::RepeatingCallback<void(int)> on_toggle,
    base::RepeatingCallback<void(int, const gfx::Point&)> on_context,
    base::RepeatingCallback<void(int, const std::string&)> on_rename)
    : folder_index_(folder_index),
      on_toggle_(std::move(on_toggle)),
      on_context_(std::move(on_context)),
      on_rename_(std::move(on_rename)) {
  SetPreferredSize(gfx::Size(0, AvoraPinnedSectionView::kRowHeight));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(0, AvoraPinnedSectionView::kHPadding),
      8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Folder icon (painted in OnPaintBackground).
  // Spacer for the icon area.
  auto icon_spacer = std::make_unique<views::View>();
  icon_spacer->SetPreferredSize(
      gfx::Size(AvoraPinnedSectionView::kFaviconSize,
                AvoraPinnedSectionView::kFaviconSize));
  icon_spacer_ = AddChildView(std::move(icon_spacer));

  // Folder name label (visible in normal mode).
  auto label = std::make_unique<views::Label>(
      base::UTF8ToUTF16(name),
      views::Label::CustomFont{gfx::FontList({std::string("system-ui")},
                                              gfx::Font::NORMAL, 12,
                                              gfx::Font::Weight::MEDIUM)});
  label->SetEnabledColor(kLabelColor);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  name_label_ = AddChildView(std::move(label));
  layout->SetFlexForView(name_label_, 1);

  // Inline rename textfield (hidden until double-click).
  auto field = std::make_unique<RenameTextfield>(
      base::BindRepeating(&FolderHeaderView::CommitRename,
                          base::Unretained(this)));
  field->SetText(base::UTF8ToUTF16(name));
  field->set_controller(this);
  field->SetFontList(gfx::FontList({std::string("system-ui")},
                                    gfx::Font::NORMAL, 12,
                                    gfx::Font::Weight::MEDIUM));
  field->SetColor(kLabelColor);
  field->SetBackgroundColor(ui::ColorVariant(SkColorSetARGB(0x30, 0xFF, 0xFF, 0xFF)));
  field->SetVisible(false);
  rename_field_ = AddChildView(std::unique_ptr<views::Textfield>(field.release()));
  layout->SetFlexForView(rename_field_, 1);

}

FolderHeaderView::~FolderHeaderView() = default;

void FolderHeaderView::SetHighlighted(bool highlighted) {
  if (highlighted_ == highlighted) {
    return;
  }
  highlighted_ = highlighted;
  SchedulePaint();
}

void FolderHeaderView::OnPaintBackground(gfx::Canvas* canvas) {
  // Draw a blue insertion line below the folder header during drag hover.
  if (highlighted_) {
    constexpr SkColor kLineColor = SkColorSetARGB(0xFF, 0x6E, 0xA8, 0xFF);
    constexpr int kLineThickness = 2;
    gfx::Rect bounds = GetLocalBounds();
    int line_y = bounds.bottom() - kLineThickness;
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(kLineColor);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRect(
        gfx::RectF(AvoraPinnedSectionView::kHPadding, line_y,
                    bounds.width() - AvoraPinnedSectionView::kHPadding * 2,
                    kLineThickness),
        flags);
  }

  // Paint the folder icon aligned to the icon spacer's layout position.
  if (icon_spacer_) {
    const gfx::Rect r = icon_spacer_->bounds();
    PaintFolderIcon(canvas, r.x(), r.y(),
                    AvoraPinnedSectionView::kFaviconSize, kFolderIconColor);
  }
}

bool FolderHeaderView::OnMousePressed(const ui::MouseEvent& event) {
  if (is_renaming_) {
    return false;
  }
  if (event.IsRightMouseButton()) {
    gfx::Point screen_point = event.location();
    ConvertPointToScreen(this, &screen_point);
    if (on_context_) {
      on_context_.Run(folder_index_, screen_point);
    }
    return true;
  }
  if (event.IsLeftMouseButton()) {
    if (event.GetClickCount() == 2) {
      BeginRename();
      return true;
    }
    if (on_toggle_) {
      on_toggle_.Run(folder_index_);
    }
    return true;
  }
  return false;
}

void FolderHeaderView::BeginRename() {
  if (is_renaming_) {
    return;
  }
  is_renaming_ = true;
  name_label_->SetVisible(false);
  rename_field_->SetVisible(true);
  rename_field_->SelectAll(true);
  rename_field_->RequestFocus();
  InvalidateLayout();
}

bool FolderHeaderView::HandleKeyEvent(views::Textfield* sender,
                                      const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    CommitRename();
    return true;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    CancelRename();
    return true;
  }
  return false;
}

void FolderHeaderView::CommitRename() {
  if (!is_renaming_) {
    return;
  }
  is_renaming_ = false;
  std::string new_name = base::UTF16ToUTF8(rename_field_->GetText());
  rename_field_->SetVisible(false);
  name_label_->SetVisible(true);
  if (!new_name.empty()) {
    name_label_->SetText(base::UTF8ToUTF16(new_name));
  }
  InvalidateLayout();
  // Defer the rename callback because it triggers Rebuild() which
  // destroys this view.  Running it synchronously would be a
  // use-after-free.
  if (!new_name.empty() && on_rename_) {
    auto callback = on_rename_;
    int idx = folder_index_;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::RepeatingCallback<void(int, const std::string&)> cb,
               int folder_idx, std::string name) {
              cb.Run(folder_idx, name);
            },
            callback, idx, new_name));
  }
}

void FolderHeaderView::CancelRename() {
  if (!is_renaming_) {
    return;
  }
  is_renaming_ = false;
  rename_field_->SetText(name_label_->GetText());
  rename_field_->SetVisible(false);
  name_label_->SetVisible(true);
  InvalidateLayout();
}

BEGIN_METADATA(FolderHeaderView)
END_METADATA

// ---------------------------------------------------------------------------
// FolderTabRow
// ---------------------------------------------------------------------------

FolderTabRow::FolderTabRow(
    int folder_index,
    const std::string& url,
    const std::string& title,
    base::RepeatingCallback<void(const std::string&)> on_click,
    base::RepeatingCallback<void(int, const std::string&,
                                 const gfx::Point&)> on_context)
    : folder_index_(folder_index),
      url_(url),
      title_(title),
      on_click_(std::move(on_click)),
      on_context_(std::move(on_context)) {
  SetPreferredSize(gfx::Size(0, 32));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(0, AvoraPinnedSectionView::kHPadding +
                             AvoraPinnedSectionView::kIndent),
      8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Favicon — constrain both the view bounds and the rendered image to 16px.
  auto icon = std::make_unique<views::ImageView>();
  icon->SetImageSize(
      gfx::Size(AvoraPinnedSectionView::kFaviconSize,
                AvoraPinnedSectionView::kFaviconSize));
  icon->SetPreferredSize(
      gfx::Size(AvoraPinnedSectionView::kFaviconSize,
                AvoraPinnedSectionView::kFaviconSize));
  favicon_view_ = AddChildView(std::move(icon));

  // Title label — use font size 14 to match daily tabs.
  auto label = std::make_unique<views::Label>(
      base::UTF8ToUTF16(title.empty() ? url : title),
      views::Label::CustomFont{gfx::FontList({std::string("system-ui")},
                                              gfx::Font::NORMAL, 14,
                                              gfx::Font::Weight::NORMAL)});
  label->SetEnabledColor(kLabelColor);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  auto* label_ptr = AddChildView(std::move(label));
  layout->SetFlexForView(label_ptr, 1);

  SetTooltipText(base::UTF8ToUTF16(title.empty() ? url : title));
  SetNotifyEnterExitOnChild(true);
}

FolderTabRow::~FolderTabRow() = default;

void FolderTabRow::SetFavicon(const gfx::ImageSkia& icon) {
  if (icon.isNull() || !favicon_view_) {
    return;
  }
  gfx::ImageSkia resized = gfx::ImageSkiaOperations::CreateResizedImage(
      icon, skia::ImageOperations::RESIZE_BEST,
      gfx::Size(AvoraPinnedSectionView::kFaviconSize,
                AvoraPinnedSectionView::kFaviconSize));
  favicon_view_->SetImage(ui::ImageModel::FromImageSkia(resized));
}

void FolderTabRow::OnPaintBackground(gfx::Canvas* canvas) {
  // Subtle hover-style background.
  if (IsMouseHovered()) {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(kRowHoverBg);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRoundRect(GetLocalBounds(), 6.0f, flags);
  }
}

bool FolderTabRow::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsRightMouseButton()) {
    gfx::Point screen_point = event.location();
    ConvertPointToScreen(this, &screen_point);
    if (on_context_) {
      on_context_.Run(folder_index_, url_, screen_point);
    }
    return true;
  }
  if (event.IsLeftMouseButton() && on_click_) {
    on_click_.Run(url_);
    return true;
  }
  return false;
}

void FolderTabRow::OnMouseEntered(const ui::MouseEvent& event) {
  SchedulePaint();
}

void FolderTabRow::OnMouseExited(const ui::MouseEvent& event) {
  SchedulePaint();
}

BEGIN_METADATA(FolderTabRow)
END_METADATA

// ---------------------------------------------------------------------------
// PinnedTabRow
// ---------------------------------------------------------------------------

PinnedTabRow::PinnedTabRow(
    content::WebContents* contents,
    base::RepeatingCallback<void(content::WebContents*)> on_click,
    base::RepeatingCallback<void(content::WebContents*,
                                 const gfx::Point&)> on_context,
    PinnedTabRow::RenameCallback on_rename)
    : contents_(contents),
      on_click_(std::move(on_click)),
      on_context_(std::move(on_context)),
      on_rename_(std::move(on_rename)) {
  SetPreferredSize(gfx::Size(0, AvoraPinnedSectionView::kRowHeight));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(0, AvoraPinnedSectionView::kHPadding),
      8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Favicon — constrain both the view bounds and the rendered image to 16px.
  auto icon = std::make_unique<views::ImageView>();
  icon->SetImageSize(
      gfx::Size(AvoraPinnedSectionView::kFaviconSize,
                AvoraPinnedSectionView::kFaviconSize));
  icon->SetPreferredSize(
      gfx::Size(AvoraPinnedSectionView::kFaviconSize,
                AvoraPinnedSectionView::kFaviconSize));
  favicon_view_ = AddChildView(std::move(icon));

  // Title label — use font size 14 to match daily tabs.
  std::u16string title = contents_->GetTitle();
  if (title.empty()) {
    title = base::UTF8ToUTF16(contents_->GetLastCommittedURL().spec());
  }
  auto label = std::make_unique<views::Label>(
      title,
      views::Label::CustomFont{gfx::FontList({std::string("system-ui")},
                                              gfx::Font::NORMAL, 14,
                                              gfx::Font::Weight::NORMAL)});
  label->SetEnabledColor(kLabelColor);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  title_label_ = AddChildView(std::move(label));
  layout->SetFlexForView(title_label_, 1);

  // Inline rename textfield (hidden until double-click).
  auto field = std::make_unique<RenameTextfield>(
      base::BindRepeating(&PinnedTabRow::CommitRename,
                          base::Unretained(this)));
  field->SetText(title);
  field->set_controller(this);
  field->SetFontList(gfx::FontList({std::string("system-ui")},
                                    gfx::Font::NORMAL, 14,
                                    gfx::Font::Weight::NORMAL));
  field->SetColor(kLabelColor);
  field->SetBackgroundColor(SK_ColorTRANSPARENT);
  field->SetBorder(nullptr);
  field->SetVisible(false);
  rename_field_ = AddChildView(std::unique_ptr<views::Textfield>(field.release()));
  layout->SetFlexForView(rename_field_, 1);

  SetTooltipText(title);
  SetNotifyEnterExitOnChild(true);
}

PinnedTabRow::~PinnedTabRow() {
  // Clear the textfield controller and blur callback before View::~View()
  // destroys children.  Otherwise the RenameTextfield's OnBlur can fire
  // during teardown and call CommitRename on a partially-destroyed
  // PinnedTabRow.
  if (rename_field_) {
    rename_field_->set_controller(nullptr);
    static_cast<RenameTextfield*>(rename_field_.get())->ClearOnBlur();
  }
}

void PinnedTabRow::SetFavicon(const gfx::ImageSkia& icon) {
  if (icon.isNull() || !favicon_view_) {
    return;
  }
  gfx::ImageSkia resized = gfx::ImageSkiaOperations::CreateResizedImage(
      icon, skia::ImageOperations::RESIZE_BEST,
      gfx::Size(AvoraPinnedSectionView::kFaviconSize,
                AvoraPinnedSectionView::kFaviconSize));
  favicon_view_->SetImage(ui::ImageModel::FromImageSkia(resized));
}

void PinnedTabRow::SetCustomTitle(const std::u16string& title) {
  if (title_label_ && !title.empty()) {
    title_label_->SetText(title);
    SetTooltipText(title);
  }
}

void PinnedTabRow::UpdateTitle() {
  if (!contents_ || !title_label_) {
    return;
  }
  std::u16string title = contents_->GetTitle();
  if (title.empty()) {
    title = base::UTF8ToUTF16(contents_->GetLastCommittedURL().spec());
  }
  title_label_->SetText(title);
  SetTooltipText(title);
}

void PinnedTabRow::OnPaintBackground(gfx::Canvas* canvas) {
  if (IsMouseHovered()) {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(kRowHoverBg);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRoundRect(GetLocalBounds(), 6.0f, flags);
  }
}

void PinnedTabRow::StartRename() {
  if (is_renaming_ || !title_label_) {
    return;
  }
  is_renaming_ = true;
  title_label_->SetVisible(false);
  rename_field_->SetText(title_label_->GetText());
  rename_field_->SetVisible(true);
  rename_field_->SelectAll(true);
  InvalidateLayout();
  rename_field_->RequestFocus();
}

void PinnedTabRow::CommitRename() {
  if (!is_renaming_ || !rename_field_) {
    return;
  }
  std::u16string new_title(rename_field_->GetText());
  is_renaming_ = false;

  // Clear focus first so the cursor stops blinking immediately and the
  // blur callback doesn't re-enter CommitRename.
  if (rename_field_->HasFocus()) {
    static_cast<RenameTextfield*>(rename_field_.get())->ClearOnBlur();
    if (auto* focus_manager = GetFocusManager()) {
      focus_manager->ClearFocus();
    }
  }

  rename_field_->SetVisible(false);
  if (title_label_) {
    if (!new_title.empty()) {
      title_label_->SetText(new_title);
    }
    title_label_->SetVisible(true);
  }
  InvalidateLayout();

  // Persist the title to prefs.  SetCustomTabTitle no longer triggers
  // Rebuild(), so calling synchronously is safe and makes the rename
  // feel instant.
  if (on_rename_ && contents_ && !new_title.empty()) {
    on_rename_.Run(contents_, new_title);
  }
}

bool PinnedTabRow::HandleKeyEvent(views::Textfield* sender,
                                  const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    CommitRename();
    return true;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    CancelRename();
    return true;
  }
  return false;
}

void PinnedTabRow::CancelRename() {
  if (!is_renaming_) {
    return;
  }
  is_renaming_ = false;

  if (rename_field_->HasFocus()) {
    static_cast<RenameTextfield*>(rename_field_.get())->ClearOnBlur();
    if (auto* focus_manager = GetFocusManager()) {
      focus_manager->ClearFocus();
    }
  }

  rename_field_->SetText(title_label_->GetText());
  rename_field_->SetVisible(false);
  title_label_->SetVisible(true);
  InvalidateLayout();
}

bool PinnedTabRow::OnMousePressed(const ui::MouseEvent& event) {
  if (is_renaming_) {
    return false;
  }
  if (event.IsRightMouseButton()) {
    gfx::Point screen_point = event.location();
    ConvertPointToScreen(this, &screen_point);
    if (on_context_) {
      on_context_.Run(contents_, screen_point);
    }
    return true;
  }
  if (event.IsLeftMouseButton()) {
    if (event.GetClickCount() == 2) {
      StartRename();
      return true;
    }
    if (on_click_) {
      on_click_.Run(contents_);
    }
    return true;
  }
  return false;
}

void PinnedTabRow::OnMouseEntered(const ui::MouseEvent& event) {
  SchedulePaint();
}

void PinnedTabRow::OnMouseExited(const ui::MouseEvent& event) {
  SchedulePaint();
}

BEGIN_METADATA(PinnedTabRow)
END_METADATA

// ---------------------------------------------------------------------------
// AvoraPinnedSectionView
// ---------------------------------------------------------------------------

AvoraPinnedSectionView::AvoraPinnedSectionView(
    BrowserWindowInterface* browser,
    avora::WindowSpaceState* window_space_state)
    : browser_(browser), window_space_state_(window_space_state) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));

  set_context_menu_controller(this);

  Profile* profile = browser_->GetProfile();
  folders_manager_ =
      std::make_unique<PinnedFoldersManager>(profile->GetPrefs());
  folders_manager_->AddObserver(this);

  pinned_items_manager_ =
      std::make_unique<PinnedItemsManager>(profile->GetPrefs());
  pinned_items_manager_->AddObserver(this);

  if (window_space_state_) {
    folders_manager_->SetWindowActiveSpaceId(
        window_space_state_->active_space_id());
    pinned_items_manager_->SetWindowActiveSpaceId(
        window_space_state_->active_space_id());
    window_space_state_->AddObserver(this);
  }

  tab_strip_model_ = browser_->GetTabStripModel();
  if (tab_strip_model_) {
    tab_strip_model_->AddObserver(this);
  }

  Rebuild();
}

AvoraPinnedSectionView::~AvoraPinnedSectionView() {
  if (window_space_state_) {
    window_space_state_->RemoveObserver(this);
  }
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
  if (folders_manager_) {
    folders_manager_->RemoveObserver(this);
  }
  if (pinned_items_manager_) {
    pinned_items_manager_->RemoveObserver(this);
  }
}

void AvoraPinnedSectionView::OnPinnedFoldersChanged() {
  Rebuild();
}

void AvoraPinnedSectionView::OnPinnedItemsChanged() {
  Rebuild();
}

void AvoraPinnedSectionView::OnWindowActiveSpaceChanged(
    const std::string& space_id) {
  if (folders_manager_) {
    folders_manager_->SetWindowActiveSpaceId(space_id);
  }
  if (pinned_items_manager_) {
    // Switching Spaces only changes which persisted items Rebuild() reads
    // (via OnPinnedItemsChanged() -> Rebuild()); it never touches the tab
    // strip, so it can never materialize anything.
    pinned_items_manager_->SetWindowActiveSpaceId(space_id);
  }
}

void AvoraPinnedSectionView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::kInserted ||
      change.type() == TabStripModelChange::kRemoved) {
    Rebuild();
  }
}

void AvoraPinnedSectionView::OnTabPinnedStateChanged(
    tabs::TabInterface* tab,
    int index) {
  Rebuild();
}

void AvoraPinnedSectionView::OnTabChangedAt(tabs::TabInterface* tab,
                                            TabChangeType change_type) {
  if (change_type == TabChangeType::kAll) {
    Rebuild();
  }
}

void AvoraPinnedSectionView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  context_menu_delegate_ = std::make_unique<SectionContextMenuDelegate>(
      base::BindOnce(
          [](PinnedFoldersManager* mgr) {
            mgr->AddFolder("New Folder");
          },
          base::Unretained(folders_manager_.get())));

  context_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(context_menu_delegate_.get());
  context_menu_model_->AddItem(kContextMenuAddFolderId, u"New Folder");

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(GetWidget(), nullptr,
                                  gfx::Rect(point, gfx::Size()),
                                  views::MenuAnchorPosition::kTopLeft,
                                  source_type);
}

gfx::Size AvoraPinnedSectionView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  gfx::Size size = views::View::CalculatePreferredSize(available_size);
  // Always show a minimum-height area so the user can right-click to
  // create a folder even when the section is otherwise empty.
  if (size.height() < kRowHeight) {
    size.set_height(kRowHeight);
  }
  return size;
}

void AvoraPinnedSectionView::OnPaintBackground(gfx::Canvas* canvas) {
  if (!is_drop_highlighted_) {
    return;
  }

  constexpr SkColor kLineColor = SkColorSetARGB(0xFF, 0x6E, 0xA8, 0xFF);
  constexpr int kLineThickness = 2;

  // Draw a blue insertion line at the bottom of the pinned section to
  // indicate "tab will be pinned here".
  gfx::Rect bounds = GetLocalBounds();
  int line_y = bounds.bottom() - kLineThickness;
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(kLineColor);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawRect(
      gfx::RectF(kHPadding, line_y,
                  bounds.width() - kHPadding * 2, kLineThickness),
      flags);
}

void AvoraPinnedSectionView::Rebuild() {
  RemoveAllChildViews();

  const std::string active_space = folders_manager_->GetActiveSpaceId();

  // Pinned-item ids with a live PinnedTabRow rendered in section 1, so
  // section 1b never renders a second, unmaterialized row for the same item.
  std::set<std::string> already_live;

  // 1. Live rows: every tab in this window/Space that is either natively
  //    pinned (TabStripModel::IsTabPinned, i.e. within
  //    IndexOfFirstNonPinnedTab()) or materialized from a persisted kPinned
  //    item (IsPinnedItemTab), or both (a natively pinned tab that a
  //    kPinned item was backfilled onto). Each gets exactly one
  //    PinnedTabRow regardless of which of those is true -- there is one
  //    intentional sidebar representation per tab, never two.
  //
  //    ApplyVisibility() (avora_space_tab_filter.cc) is the other half of
  //    this: it hides a materialized-but-not-natively-pinned tab's row from
  //    the daily/unpinned tab strip, the same way it already hides a
  //    Favorite's tab there, so the tab doesn't ALSO show as a normal daily
  //    row while it has a live PinnedTabRow here. A natively pinned tab's
  //    own strip row is untouched by that rule regardless of kPinned
  //    backing -- only this section's representation changes for it.
  if (tab_strip_model_) {
    const int first_unpinned = tab_strip_model_->IndexOfFirstNonPinnedTab();
    for (int i = 0; i < tab_strip_model_->count(); ++i) {
      content::WebContents* contents = tab_strip_model_->GetWebContentsAt(i);
      if (!contents || !TabBelongsToSpace(contents, active_space)) {
        continue;
      }

      const bool natively_pinned = i < first_unpinned;
      const std::string pinned_item_id = GetPinnedItemIdForTab(contents);
      if (!natively_pinned && pinned_item_id.empty()) {
        continue;  // An ordinary daily tab -- not this section's concern.
      }
      if (!pinned_item_id.empty()) {
        already_live.insert(pinned_item_id);
      }

      auto* row = AddChildView(std::make_unique<PinnedTabRow>(
          contents,
          base::BindRepeating(&AvoraPinnedSectionView::OnPinnedTabClicked,
                              base::Unretained(this)),
          base::BindRepeating(
              &AvoraPinnedSectionView::OnPinnedTabContextMenu,
              base::Unretained(this)),
          base::BindRepeating(
              &AvoraPinnedSectionView::OnPinnedTabRenamed,
              base::Unretained(this))));
      // Use custom title if one was saved.
      std::string url = contents->GetLastCommittedURL().spec();
      std::string custom = folders_manager_->GetCustomTabTitle(url);
      if (!custom.empty()) {
        row->SetCustomTitle(base::UTF8ToUTF16(custom));
      }
      LoadPinnedTabFavicon(row);
    }
  }

  // 1b. Persisted pinned items (SidebarItemType::kPinned) with no live tab in
  //     this window yet. Never eagerly materializes anything -- this only
  //     reads PinnedItemsManager's persisted list and renders a row per
  //     entry; a live tab is created lazily, on click, in
  //     OnPersistentPinnedItemClicked(). An item already shown live in
  //     section 1 above is skipped here so it never renders twice.
  if (pinned_items_manager_) {
    for (const auto& item : pinned_items_manager_->GetPinnedItems()) {
      if (already_live.count(item.id)) {
        continue;
      }
      auto* row = AddChildView(std::make_unique<FolderTabRow>(
          /*folder_index=*/-1, item.url, item.title,
          base::BindRepeating(
              &AvoraPinnedSectionView::OnPersistentPinnedItemClicked,
              base::Unretained(this)),
          base::RepeatingCallback<void(int, const std::string&,
                                       const gfx::Point&)>()));
      LoadFavicon(row);
    }
  }

  // 2. Folders and their contained tabs below.
  auto folders = folders_manager_->GetFolders();
  for (int i = 0; i < static_cast<int>(folders.size()); ++i) {
    const auto& folder = folders[i];

    // Folder header.
    auto* header = AddChildView(std::make_unique<FolderHeaderView>(
        i, folder.name, folder.expanded,
        base::BindRepeating(&AvoraPinnedSectionView::OnFolderToggle,
                            base::Unretained(this)),
        base::BindRepeating(&AvoraPinnedSectionView::OnFolderContextMenu,
                            base::Unretained(this)),
        base::BindRepeating(&AvoraPinnedSectionView::OnFolderRename,
                            base::Unretained(this))));

    // Restore highlight if a drag is active.
    if (highlighted_folder_index_ == i) {
      header->SetHighlighted(true);
    }

    // Tab rows (visible only when folder is expanded).
    if (folder.expanded) {
      for (const auto& tab : folder.tabs) {
        auto* row = AddChildView(std::make_unique<FolderTabRow>(
            i, tab.url, tab.title,
            base::BindRepeating(&AvoraPinnedSectionView::OnTabClicked,
                                base::Unretained(this)),
            base::BindRepeating(&AvoraPinnedSectionView::OnTabContextMenu,
                                base::Unretained(this))));
        LoadFavicon(row);
      }
    }
  }

  SetVisible(true);
  InvalidateLayout();
  if (parent()) {
    parent()->InvalidateLayout();
  }
}

PinnedFoldersManager* AvoraPinnedSectionView::GetFoldersManager() const {
  return folders_manager_.get();
}

PinnedItemsManager* AvoraPinnedSectionView::GetPinnedItemsManager() const {
  return pinned_items_manager_.get();
}

void AvoraPinnedSectionView::SetDropHighlighted(bool highlighted) {
  if (is_drop_highlighted_ == highlighted) {
    return;
  }
  is_drop_highlighted_ = highlighted;
  SchedulePaint();
}

void AvoraPinnedSectionView::SetDragActive(bool active) {
  if (is_drag_active_ == active) {
    return;
  }
  is_drag_active_ = active;
  if (!active) {
    ClearHighlight();
  }
  SetVisible(true);
  InvalidateLayout();
  if (parent()) {
    parent()->InvalidateLayout();
  }
}

int AvoraPinnedSectionView::GetFolderIndexAtScreenPoint(
    const gfx::Point& screen_point) const {
  for (views::View* child : children()) {
    auto* header = views::AsViewClass<FolderHeaderView>(child);
    if (header && header->GetVisible() &&
        header->GetBoundsInScreen().Contains(screen_point)) {
      return header->folder_index();
    }
  }
  return -1;
}

void AvoraPinnedSectionView::HighlightFolder(int folder_index) {
  if (highlighted_folder_index_ == folder_index) {
    return;
  }
  // Clear previous highlight.
  ClearHighlight();
  highlighted_folder_index_ = folder_index;
  if (folder_index >= 0) {
    for (views::View* child : children()) {
      auto* header = views::AsViewClass<FolderHeaderView>(child);
      if (header && header->folder_index() == folder_index) {
        header->SetHighlighted(true);
        break;
      }
    }
  }
}

void AvoraPinnedSectionView::ClearHighlight() {
  if (highlighted_folder_index_ < 0) {
    return;
  }
  for (views::View* child : children()) {
    auto* header = views::AsViewClass<FolderHeaderView>(child);
    if (header && header->folder_index() == highlighted_folder_index_) {
      header->SetHighlighted(false);
      break;
    }
  }
  highlighted_folder_index_ = -1;
}

void AvoraPinnedSectionView::OnFolderToggle(int folder_index) {
  auto folders = folders_manager_->GetFolders();
  if (folder_index >= 0 &&
      folder_index < static_cast<int>(folders.size())) {
    folders_manager_->SetFolderExpanded(folder_index,
                                        !folders[folder_index].expanded);
  }
}

void AvoraPinnedSectionView::OnFolderRename(int folder_index,
                                            const std::string& new_name) {
  folders_manager_->RenameFolder(folder_index, new_name);
}

void AvoraPinnedSectionView::OnFolderContextMenu(
    int folder_index,
    const gfx::Point& screen_point) {
  context_menu_delegate_ = std::make_unique<FolderContextMenuDelegate>(
      base::BindOnce(
          [](base::WeakPtr<AvoraPinnedSectionView> view, int idx) {
            if (!view) return;
            for (views::View* child : view->children()) {
              auto* header = views::AsViewClass<FolderHeaderView>(child);
              if (header && header->folder_index() == idx) {
                header->BeginRename();
                break;
              }
            }
          },
          weak_factory_.GetWeakPtr(), folder_index),
      base::BindOnce(
          [](PinnedFoldersManager* mgr, int idx) {
            mgr->RemoveFolder(idx);
          },
          base::Unretained(folders_manager_.get()), folder_index),
      base::BindOnce(
          [](PinnedFoldersManager* mgr) {
            mgr->AddFolder("New Folder");
          },
          base::Unretained(folders_manager_.get())));

  context_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(context_menu_delegate_.get());
  context_menu_model_->AddItem(kContextMenuRenameFolderId, u"Rename Folder");
  context_menu_model_->AddItem(kContextMenuDeleteFolderId, u"Delete Folder");
  context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  context_menu_model_->AddItem(kContextMenuAddFolderId, u"New Folder");

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(GetWidget(), nullptr,
                                  gfx::Rect(screen_point, gfx::Size()),
                                  views::MenuAnchorPosition::kTopLeft,
                                  ui::mojom::MenuSourceType::kMouse);
}

void AvoraPinnedSectionView::OnTabClicked(const std::string& url) {
  if (!browser_) {
    return;
  }

  TabStripModel* tab_strip = browser_->GetTabStripModel();
  if (!tab_strip) {
    return;
  }

  // Find and activate existing tab with this URL in the active Space.
  const std::string active_space = folders_manager_->GetActiveSpaceId();
  for (int i = 0; i < tab_strip->count(); ++i) {
    content::WebContents* contents = tab_strip->GetWebContentsAt(i);
    if (contents && contents->GetLastCommittedURL().spec() == url &&
        TabBelongsToSpace(contents, active_space)) {
      tab_strip->ActivateTabAt(i);
      return;
    }
  }

  // Tab not open — open it.
  NavigateParams params(browser_, GURL(url),
                        ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

void AvoraPinnedSectionView::OnPersistentPinnedItemClicked(
    const std::string& url) {
  if (!browser_ || !tab_strip_model_ || !pinned_items_manager_) {
    return;
  }

  // Resolve the click to the pinned item's identity rather than comparing
  // URLs from here on, mirroring AvoraFavoritesView::OnFavoriteClicked: a
  // tab that has navigated away from the item's persisted target is still
  // recognised as belonging to it.
  const std::string item_id = pinned_items_manager_->GetPinnedItemIdForUrl(url);
  if (item_id.empty()) {
    return;
  }

  const std::string active_space = pinned_items_manager_->GetActiveSpaceId();

  // Scoped to this window's own TabStripModel only: a match here can never
  // resolve to (and this can never activate) a tab in another window.
  const int existing_index = FindMaterializedPinnedItemTab(
      tab_strip_model_, item_id, active_space);
  if (existing_index != TabStripModel::kNoTab) {
    tab_strip_model_->ActivateTabAt(existing_index);
    return;
  }

  // No live tab for this item in this window/Space yet -- materialize one
  // from the item's persisted target, not from |url| (they are the same
  // value today, but the persisted target is the source of truth).
  const std::string target_url =
      pinned_items_manager_->GetPinnedItemUrlById(item_id);
  if (target_url.empty()) {
    return;
  }

  NavigateParams params(browser_, GURL(target_url),
                        ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);

  if (params.navigated_or_inserted_contents) {
    MarkPinnedItemTab(params.navigated_or_inserted_contents, item_id);
    if (!active_space.empty()) {
      SetTabSpaceId(params.navigated_or_inserted_contents, active_space);
    }
  }
}

void AvoraPinnedSectionView::OnTabContextMenu(
    int folder_index,
    const std::string& url,
    const gfx::Point& screen_point) {
  context_menu_delegate_ = std::make_unique<TabRowContextMenuDelegate>(
      base::BindOnce(
          [](PinnedFoldersManager* mgr, int idx, const std::string& url) {
            mgr->RemoveTabFromFolder(idx, url);
          },
          base::Unretained(folders_manager_.get()), folder_index, url));

  context_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(context_menu_delegate_.get());
  context_menu_model_->AddItem(kContextMenuRemoveFromFolderId,
                               u"Remove from Folder");

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(GetWidget(), nullptr,
                                  gfx::Rect(screen_point, gfx::Size()),
                                  views::MenuAnchorPosition::kTopLeft,
                                  ui::mojom::MenuSourceType::kMouse);
}

void AvoraPinnedSectionView::OnPinnedTabClicked(
    content::WebContents* contents) {
  if (!tab_strip_model_ || !contents) {
    return;
  }
  int index = tab_strip_model_->GetIndexOfWebContents(contents);
  if (index != TabStripModel::kNoTab) {
    tab_strip_model_->ActivateTabAt(index);
  }
}

void AvoraPinnedSectionView::OnPinnedTabRenamed(
    content::WebContents* contents,
    const std::u16string& new_title) {
  if (!contents || !folders_manager_) {
    return;
  }
  std::string url = contents->GetLastCommittedURL().spec();
  folders_manager_->SetCustomTabTitle(url, base::UTF16ToUTF8(new_title));
}

void AvoraPinnedSectionView::OnPinnedTabContextMenu(
    content::WebContents* contents,
    const gfx::Point& screen_point) {
  if (!tab_strip_model_ || !contents) {
    return;
  }
  context_menu_delegate_ = std::make_unique<PinnedTabContextMenuDelegate>(
      base::BindOnce(
          [](base::WeakPtr<AvoraPinnedSectionView> view,
             content::WebContents* wc) {
            if (!view) {
              return;
            }
            // Clears the native pinned bit if set and independently removes
            // any kPinned persistence -- one "Unpin Tab" action fully
            // unpins the tab in every sense, rather than leaving it
            // half-unpinned depending on how it came to be pinned. The live
            // page is never closed.
            UnpinAndRemovePinnedItem(view->tab_strip_model_,
                                     view->pinned_items_manager_.get(), wc);
          },
          weak_factory_.GetWeakPtr(), contents),
      base::BindOnce(
          [](PinnedFoldersManager* mgr) {
            mgr->AddFolder("New Folder");
          },
          base::Unretained(folders_manager_.get())));

  context_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(context_menu_delegate_.get());
  context_menu_model_->AddItem(kContextMenuUnpinTabId, u"Unpin Tab");
  context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  context_menu_model_->AddItem(kContextMenuAddFolderId, u"New Folder");

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(GetWidget(), nullptr,
                                  gfx::Rect(screen_point, gfx::Size()),
                                  views::MenuAnchorPosition::kTopLeft,
                                  ui::mojom::MenuSourceType::kMouse);
}

void AvoraPinnedSectionView::LoadPinnedTabFavicon(PinnedTabRow* row) {
  if (!browser_ || !row->web_contents()) {
    return;
  }
  favicon::FaviconService* favicon_service =
      FaviconServiceFactory::GetForProfile(
          browser_->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
  if (!favicon_service) {
    return;
  }

  std::string url = row->web_contents()->GetLastCommittedURL().spec();
  favicon_service->GetRawFaviconForPageURL(
      GURL(url),
      {favicon_base::IconType::kFavicon},
      kFaviconSize,
      /*fallback_to_host=*/true,
      base::BindOnce(
          [](base::WeakPtr<AvoraPinnedSectionView> view,
             content::WebContents* wc,
             const favicon_base::FaviconRawBitmapResult& result) {
            if (!view || !result.is_valid()) {
              return;
            }
            gfx::ImageSkia icon =
                gfx::Image::CreateFrom1xPNGBytes(result.bitmap_data)
                    .AsImageSkia();
            if (icon.isNull()) {
              return;
            }
            for (views::View* child : view->children()) {
              auto* row = views::AsViewClass<PinnedTabRow>(child);
              if (row && row->web_contents() == wc) {
                row->SetFavicon(icon);
                break;
              }
            }
          },
          weak_factory_.GetWeakPtr(), row->web_contents()),
      &cancelable_task_tracker_);
}

void AvoraPinnedSectionView::LoadFavicon(FolderTabRow* row) {
  if (!browser_) {
    return;
  }
  favicon::FaviconService* favicon_service =
      FaviconServiceFactory::GetForProfile(
          browser_->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
  if (!favicon_service) {
    return;
  }

  favicon_service->GetRawFaviconForPageURL(
      GURL(row->url()),
      {favicon_base::IconType::kFavicon},
      kFaviconSize,
      /*fallback_to_host=*/true,
      base::BindOnce(
          [](base::WeakPtr<AvoraPinnedSectionView> view,
             const std::string& url,
             const favicon_base::FaviconRawBitmapResult& result) {
            if (!view || !result.is_valid()) {
              return;
            }
            gfx::ImageSkia icon =
                gfx::Image::CreateFrom1xPNGBytes(result.bitmap_data)
                    .AsImageSkia();
            if (icon.isNull()) {
              return;
            }
            for (views::View* child : view->children()) {
              auto* row = views::AsViewClass<FolderTabRow>(child);
              if (row && row->url() == url) {
                row->SetFavicon(icon);
                break;
              }
            }
          },
          weak_factory_.GetWeakPtr(), row->url()),
      &cancelable_task_tracker_);
}

BEGIN_METADATA(AvoraPinnedSectionView)
END_METADATA

}  // namespace avora
