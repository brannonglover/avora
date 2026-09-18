// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_pinned_section_view.h"

#include <algorithm>
#include <map>
#include <optional>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/avora/avora_tab_space.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/avora/avora_import_dialog_view.h"
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
constexpr int kContextMenuMoveToSpaceId = 6;
constexpr int kContextMenuImportBookmarksId = 7;

// Command ids for the "Move to Space" submenu, one per destination Space.
// Menus dispatch by command id across the whole hierarchy, so this range has
// to stay clear of the fixed ids above.
constexpr int kContextMenuFirstSpaceId = 100;

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
  SectionContextMenuDelegate(base::OnceClosure on_add_folder,
                             base::OnceClosure on_import)
      : on_add_folder_(std::move(on_add_folder)),
        on_import_(std::move(on_import)) {}

  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == kContextMenuAddFolderId && on_add_folder_) {
      std::move(on_add_folder_).Run();
    } else if (command_id == kContextMenuImportBookmarksId && on_import_) {
      std::move(on_import_).Run();
    }
  }

 private:
  base::OnceClosure on_add_folder_;
  base::OnceClosure on_import_;
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

// Context menu delegate for standalone pinned tab "Unpin Tab" + "New Folder",
// plus "Remove from Folder" when the tab backs a folder member (the model
// only ever adds that menu item when relevant; the closure itself is cheap
// to always bind).
class PinnedTabContextMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  PinnedTabContextMenuDelegate(base::OnceClosure on_unpin,
                               base::OnceClosure on_add_folder,
                               base::OnceClosure on_remove_from_folder)
      : on_unpin_(std::move(on_unpin)),
        on_add_folder_(std::move(on_add_folder)),
        on_remove_from_folder_(std::move(on_remove_from_folder)) {}

  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == kContextMenuUnpinTabId && on_unpin_) {
      std::move(on_unpin_).Run();
    } else if (command_id == kContextMenuAddFolderId && on_add_folder_) {
      std::move(on_add_folder_).Run();
    } else if (command_id == kContextMenuRemoveFromFolderId &&
              on_remove_from_folder_) {
      std::move(on_remove_from_folder_).Run();
    }
  }

 private:
  base::OnceClosure on_unpin_;
  base::OnceClosure on_add_folder_;
  base::OnceClosure on_remove_from_folder_;
};

// The "Move to Space" submenu: one row per Space other than the one being
// shown.  Acts as its own delegate so the parent menu's delegate does not
// have to know how many Spaces exist when it is built.
class MoveToSpaceSubMenuModel : public ui::SimpleMenuModel,
                                public ui::SimpleMenuModel::Delegate {
 public:
  MoveToSpaceSubMenuModel(
      const std::vector<Space>& spaces,
      const std::string& current_space_id,
      base::RepeatingCallback<void(const std::string&)> on_pick)
      : ui::SimpleMenuModel(this), on_pick_(std::move(on_pick)) {
    for (const Space& space : spaces) {
      if (space.id == current_space_id) {
        continue;
      }
      AddItem(kContextMenuFirstSpaceId + static_cast<int>(space_ids_.size()),
              base::UTF8ToUTF16(space.name));
      space_ids_.push_back(space.id);
    }
  }

  bool has_destinations() const { return !space_ids_.empty(); }

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override {
    const size_t index =
        static_cast<size_t>(command_id - kContextMenuFirstSpaceId);
    if (index < space_ids_.size() && on_pick_) {
      on_pick_.Run(space_ids_[index]);
    }
  }

 private:
  std::vector<std::string> space_ids_;
  base::RepeatingCallback<void(const std::string&)> on_pick_;
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
    const std::string& folder_id,
    const std::string& name,
    bool expanded,
    base::RepeatingCallback<void(const std::string&)> on_toggle,
    base::RepeatingCallback<void(const std::string&, const gfx::Point&)>
        on_context,
    base::RepeatingCallback<void(const std::string&, const std::string&)>
        on_rename)
    : folder_id_(folder_id),
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
      on_context_.Run(folder_id_, screen_point);
    }
    return true;
  }
  if (event.IsLeftMouseButton()) {
    if (event.GetClickCount() == 2) {
      BeginRename();
      return true;
    }
    if (on_toggle_) {
      on_toggle_.Run(folder_id_);
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
    std::string id = folder_id_;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::RepeatingCallback<void(const std::string&,
                                            const std::string&)> cb,
               std::string folder_id, std::string name) {
              cb.Run(folder_id, name);
            },
            callback, id, new_name));
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
    const std::string& folder_id,
    const std::string& item_id,
    const std::string& url,
    const std::string& title,
    base::RepeatingCallback<void(std::string)> on_click,
    base::RepeatingCallback<void(const std::string&, const std::string&,
                                 const gfx::Point&)> on_context)
    : folder_id_(folder_id),
      item_id_(item_id),
      url_(url),
      title_(title),
      on_click_(std::move(on_click)),
      on_context_(std::move(on_context)) {
  SetPreferredSize(gfx::Size(0, 32));

  // Indent only for a real folder member.  A top-level item with no live
  // tab is also rendered with this row type (Rebuild() section 1b, folder_id
  // empty); indenting it too would make it read as a child of whatever row
  // happens to precede it.
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(0, AvoraPinnedSectionView::kHPadding +
                             (folder_id.empty()
                                  ? 0
                                  : AvoraPinnedSectionView::kIndent)),
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
  // Both handlers below can destroy this row before they return: the click
  // handler materializes a tab, and the context handler opens a menu -- each
  // reaches Rebuild(), which deletes every child row including this one.  So
  // take a local copy of the callback and of the ids it is handed, and touch
  // no member after the Run() call.  (FolderHeaderView::CommitRename() posts
  // its callback for the same reason.)
  if (event.IsRightMouseButton()) {
    gfx::Point screen_point = event.location();
    ConvertPointToScreen(this, &screen_point);
    if (on_context_) {
      auto on_context = on_context_;
      const std::string folder_id = folder_id_;
      const std::string item_id = item_id_;
      on_context.Run(folder_id, item_id, screen_point);
    }
    return true;
  }
  if (event.IsLeftMouseButton() && on_click_) {
    auto on_click = on_click_;
    on_click.Run(item_id_);
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

  // Only ever read, to list the destinations of the "Move to Space" submenu,
  // so there is nothing here to observe.
  space_manager_ = std::make_unique<SpaceManager>(profile->GetPrefs());

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
      base::BindOnce(&AvoraPinnedSectionView::CreateFolderAndBeginRename,
                     weak_factory_.GetWeakPtr()),
      base::BindOnce(&AvoraPinnedSectionView::OnImportBookmarksClicked,
                     weak_factory_.GetWeakPtr()));

  context_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(context_menu_delegate_.get());
  context_menu_model_->AddItem(kContextMenuAddFolderId, u"New Folder");
  context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  // The imported section no longer parks a permanent "Import Bookmarks…"
  // row under the pinned tabs, so this is where import stays reachable once
  // the first-run offer has been dismissed.
  context_menu_model_->AddItem(kContextMenuImportBookmarksId,
                               u"Import Bookmarks…");

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(GetWidget(), nullptr,
                                  gfx::Rect(point, gfx::Size()),
                                  views::MenuAnchorPosition::kTopLeft,
                                  source_type);
}

void AvoraPinnedSectionView::OnImportBookmarksClicked() {
  if (browser_) {
    AvoraImportDialogView::Show(browser_, window_space_state_);
  }
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
  const std::vector<PinnedFolder> folders = folders_manager_->GetFolders();

  // Which pinned items are folder members, and which folder -- computed once
  // so sections 1/1b can exclude them (a folder member renders exactly once,
  // under its folder in section 2, live or not).
  std::map<std::string, std::string> item_folder_id;
  for (const auto& folder : folders) {
    for (const auto& id : folder.ordered_item_ids) {
      item_folder_id[id] = folder.id;
    }
  }

  // Pinned-item ids with a live PinnedTabRow rendered in section 1, so
  // section 1b never renders a second, unmaterialized row for the same item.
  std::set<std::string> already_live;

  // A pinned row's user-chosen name is stored per URL (see
  // OnPinnedTabRenamed), never on the item, so an unmaterialized row has to
  // consult it too -- otherwise the same pin shows the name the user gave it
  // while a live tab exists and reverts to the item's original title once
  // that tab is gone.
  auto display_title = [this](const std::string& url,
                              const std::string& title) {
    const std::string custom = folders_manager_->GetCustomTabTitle(url);
    return custom.empty() ? title : custom;
  };

  // 1. Live, top-level rows: every tab in this window/Space that is either
  //    natively pinned (TabStripModel::IsTabPinned, i.e. within
  //    IndexOfFirstNonPinnedTab()) or materialized from a persisted kPinned
  //    item (IsPinnedItemTab), or both (a natively pinned tab that a
  //    kPinned item was backfilled onto) -- and not a folder member (folder
  //    members render under their folder in section 2 instead, live or
  //    not). Each gets exactly one PinnedTabRow -- one intentional sidebar
  //    representation per tab, never two.
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
      std::string pinned_item_id = GetPinnedItemIdForTab(contents);
      if (!natively_pinned && pinned_item_id.empty()) {
        continue;  // An ordinary daily tab -- not this section's concern.
      }
      if (pinned_item_id.empty() && pinned_items_manager_) {
        // A natively pinned tab carrying no marker: pinned before
        // AvoraSpaceTabFilter::OnTabPinnedStateChanged() began creating
        // kPinned records, or restored without AdoptPinnedItemTabs()
        // reaching it. Reconnect it to the persisted item for its URL the
        // same way that adoption pass does, so the tab and the item it
        // materializes never render as two separate rows -- the item's own
        // unmaterialized row in section 1b being the second one.
        pinned_item_id = pinned_items_manager_->GetPinnedItemIdForUrl(
            contents->GetLastCommittedURL().spec());
        if (!pinned_item_id.empty()) {
          MarkPinnedItemTab(contents, pinned_item_id);
        }
      }
      if (!pinned_item_id.empty()) {
        if (item_folder_id.count(pinned_item_id)) {
          continue;  // Renders under its folder in section 2 instead.
        }
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

  // 1b. Persisted top-level pinned items with no live tab in this window
  //     yet. Never eagerly materializes anything -- this only reads
  //     PinnedItemsManager's persisted list and renders a row per entry; a
  //     live tab is created lazily, on click, in
  //     OnPersistentPinnedItemClicked(). Folder members are excluded (they
  //     belong to section 2) and items already live in section 1 are
  //     excluded too, so every item renders exactly once.
  if (pinned_items_manager_) {
    for (const auto& item : pinned_items_manager_->GetPinnedItems()) {
      if (already_live.count(item.id) || item_folder_id.count(item.id)) {
        continue;
      }
      auto* row = AddChildView(std::make_unique<FolderTabRow>(
          /*folder_id=*/std::string(), item.id, item.url,
          display_title(item.url, item.title),
          base::BindRepeating(
              &AvoraPinnedSectionView::OnPersistentPinnedItemClicked,
              base::Unretained(this)),
          base::BindRepeating(
              [](base::WeakPtr<AvoraPinnedSectionView> view,
                 const std::string& /*folder_id*/,
                 const std::string& item_id, const gfx::Point& point) {
                if (view) {
                  view->OnPersistentPinnedItemContextMenu(item_id, point);
                }
              },
              weak_factory_.GetWeakPtr())));
      LoadFavicon(row);
    }
  }

  // 2. Folders, each rendering its members in ordered_item_ids order: a
  //    live row (PinnedTabRow, the exact same materialize/activate/
  //    context-menu handlers as a top-level live item -- no folder-specific
  //    tab-opening path) if a matching tab already exists in this
  //    window/Space, else an unmaterialized row (FolderTabRow) that lazily
  //    materializes one on click via the same OnPersistentPinnedItemClicked
  //    as every other Pinned item.
  for (const auto& folder : folders) {
    auto* header = AddChildView(std::make_unique<FolderHeaderView>(
        folder.id, folder.name, folder.expanded,
        base::BindRepeating(&AvoraPinnedSectionView::OnFolderToggle,
                            base::Unretained(this)),
        base::BindRepeating(&AvoraPinnedSectionView::OnFolderContextMenu,
                            base::Unretained(this)),
        base::BindRepeating(&AvoraPinnedSectionView::OnFolderRename,
                            base::Unretained(this))));

    // Restore highlight if a drag is active.
    if (highlighted_folder_id_ == folder.id) {
      header->SetHighlighted(true);
    }

    // Member rows (visible only when folder is expanded).
    if (!folder.expanded || !pinned_items_manager_) {
      continue;
    }

    for (const auto& item_id : folder.ordered_item_ids) {
      const std::optional<PinnedItemEntry> entry =
          pinned_items_manager_->GetPinnedItemById(item_id);
      if (!entry) {
        continue;  // Stale reference -- defensive only, should not happen.
      }

      const int live_index =
          tab_strip_model_ ? FindMaterializedPinnedItemTab(
                                 tab_strip_model_, item_id, active_space)
                           : TabStripModel::kNoTab;
      if (live_index != TabStripModel::kNoTab) {
        content::WebContents* contents =
            tab_strip_model_->GetWebContentsAt(live_index);
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
        std::string url = contents->GetLastCommittedURL().spec();
        std::string custom = folders_manager_->GetCustomTabTitle(url);
        if (!custom.empty()) {
          row->SetCustomTitle(base::UTF8ToUTF16(custom));
        }
        LoadPinnedTabFavicon(row);
        continue;
      }

      auto* row = AddChildView(std::make_unique<FolderTabRow>(
          folder.id, item_id, entry->url,
          display_title(entry->url, entry->title),
          base::BindRepeating(
              &AvoraPinnedSectionView::OnPersistentPinnedItemClicked,
              base::Unretained(this)),
          base::BindRepeating(&AvoraPinnedSectionView::OnFolderItemContextMenu,
                              base::Unretained(this))));
      LoadFavicon(row);
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

std::string AvoraPinnedSectionView::GetFolderIdAtScreenPoint(
    const gfx::Point& screen_point) const {
  for (views::View* child : children()) {
    auto* header = views::AsViewClass<FolderHeaderView>(child);
    if (header && header->GetVisible() &&
        header->GetBoundsInScreen().Contains(screen_point)) {
      return header->folder_id();
    }
  }
  return std::string();
}

void AvoraPinnedSectionView::HighlightFolder(const std::string& folder_id) {
  if (highlighted_folder_id_ == folder_id) {
    return;
  }
  // Clear previous highlight.
  ClearHighlight();
  highlighted_folder_id_ = folder_id;
  if (!folder_id.empty()) {
    for (views::View* child : children()) {
      auto* header = views::AsViewClass<FolderHeaderView>(child);
      if (header && header->folder_id() == folder_id) {
        header->SetHighlighted(true);
        break;
      }
    }
  }
}

void AvoraPinnedSectionView::ClearHighlight() {
  if (highlighted_folder_id_.empty()) {
    return;
  }
  for (views::View* child : children()) {
    auto* header = views::AsViewClass<FolderHeaderView>(child);
    if (header && header->folder_id() == highlighted_folder_id_) {
      header->SetHighlighted(false);
      break;
    }
  }
  highlighted_folder_id_.clear();
}

void AvoraPinnedSectionView::CreateFolderAndBeginRename() {
  if (!folders_manager_) {
    return;
  }
  const std::string folder_id = folders_manager_->AddFolder("New Folder");
  if (folder_id.empty()) {
    return;
  }

  // AddFolder() notifies observers synchronously, so this view has already
  // rebuilt and the new folder's header row exists by the time we get here.
  for (views::View* child : children()) {
    auto* header = views::AsViewClass<FolderHeaderView>(child);
    if (header && header->folder_id() == folder_id) {
      header->BeginRename();
      return;
    }
  }
}

void AvoraPinnedSectionView::OnFolderToggle(const std::string& folder_id) {
  for (const auto& folder : folders_manager_->GetFolders()) {
    if (folder.id == folder_id) {
      folders_manager_->SetFolderExpanded(folder_id, !folder.expanded);
      return;
    }
  }
}

void AvoraPinnedSectionView::OnFolderRename(const std::string& folder_id,
                                            const std::string& new_name) {
  folders_manager_->RenameFolder(folder_id, new_name);
}

void AvoraPinnedSectionView::OnFolderContextMenu(
    const std::string& folder_id,
    const gfx::Point& screen_point) {
  context_menu_delegate_ = std::make_unique<FolderContextMenuDelegate>(
      base::BindOnce(
          [](base::WeakPtr<AvoraPinnedSectionView> view, std::string id) {
            if (!view) return;
            for (views::View* child : view->children()) {
              auto* header = views::AsViewClass<FolderHeaderView>(child);
              if (header && header->folder_id() == id) {
                header->BeginRename();
                break;
              }
            }
          },
          weak_factory_.GetWeakPtr(), folder_id),
      base::BindOnce(
          [](PinnedFoldersManager* mgr, std::string id) {
            mgr->RemoveFolder(id);
          },
          base::Unretained(folders_manager_.get()), folder_id),
      base::BindOnce(&AvoraPinnedSectionView::CreateFolderAndBeginRename,
                     weak_factory_.GetWeakPtr()));

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

void AvoraPinnedSectionView::OnPersistentPinnedItemClicked(
    std::string item_id) {
  if (!browser_ || !tab_strip_model_ || !pinned_items_manager_ ||
      item_id.empty()) {
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
  // from the item's persisted target, looked up fresh here rather than
  // trusting whatever url the row happened to display.
  const std::string target_url =
      pinned_items_manager_->GetPinnedItemUrlById(item_id);
  if (target_url.empty()) {
    return;
  }

  // Reentrant: inserting the tab runs OnTabStripModelChanged(kInserted) ->
  // Rebuild() synchronously, which deletes every row in this section --
  // including the one that was just clicked.  Nothing owned by a row may be
  // read after this point, which is why |item_id| is a by-value copy.
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

void AvoraPinnedSectionView::OnFolderItemContextMenu(
    const std::string& folder_id,
    const std::string& item_id,
    const gfx::Point& screen_point) {
  context_menu_delegate_ = std::make_unique<TabRowContextMenuDelegate>(
      base::BindOnce(
          [](PinnedFoldersManager* mgr, std::string item) {
            // Empty destination = top-level, per MoveItemToFolder's
            // convention -- this is what "Remove from Folder" means now
            // that Pinned is persistent: the item survives, just no longer
            // organized under this folder.
            mgr->MoveItemToFolder(item, std::string());
          },
          base::Unretained(folders_manager_.get()), item_id));

  context_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(context_menu_delegate_.get());
  context_menu_model_->AddItem(kContextMenuRemoveFromFolderId,
                               u"Remove from Folder");

  // A folder member is a pinned tab like any other, so it gets the same
  // destination list.  Picking one takes it out of this folder on the way
  // out -- folders belong to a single Space, so membership cannot follow it
  // (see avora::MovePinnedItemToSpace).
  move_to_space_submenu_ = BuildMoveToSpaceSubMenu(
      pinned_items_manager_ ? pinned_items_manager_->GetActiveSpaceId()
                            : std::string(),
      base::BindRepeating(&AvoraPinnedSectionView::MovePinnedItemToSpace,
                          weak_factory_.GetWeakPtr(), item_id));
  if (move_to_space_submenu_) {
    context_menu_model_->AddSubMenu(kContextMenuMoveToSpaceId,
                                    u"Move to Space",
                                    move_to_space_submenu_.get());
  }

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

  // Live rows in section 2 (folder members) and section 1 (top-level) both
  // reach this same handler -- see Rebuild() -- so whether "Remove from
  // Folder" belongs on this menu is a live lookup, not something the row
  // itself knows.
  const std::string item_id = GetPinnedItemIdForTab(contents);
  const std::string folder_id =
      (!item_id.empty() && folders_manager_)
          ? folders_manager_->GetFolderIdForItem(item_id)
          : std::string();

  context_menu_delegate_ = std::make_unique<PinnedTabContextMenuDelegate>(
      base::BindOnce(
          [](base::WeakPtr<AvoraPinnedSectionView> view,
             content::WebContents* wc) {
            if (!view) {
              return;
            }
            // Clears the native pinned bit if set and independently removes
            // any kPinned persistence (including its folder membership) --
            // one "Unpin Tab" action fully unpins the tab in every sense,
            // rather than leaving it half-unpinned depending on how it came
            // to be pinned. The live page is never closed.
            UnpinAndRemovePinnedItem(view->tab_strip_model_,
                                     view->pinned_items_manager_.get(),
                                     view->folders_manager_.get(), wc);
          },
          weak_factory_.GetWeakPtr(), contents),
      base::BindOnce(&AvoraPinnedSectionView::CreateFolderAndBeginRename,
                     weak_factory_.GetWeakPtr()),
      base::BindOnce(
          [](PinnedFoldersManager* mgr, std::string item) {
            mgr->MoveItemToFolder(item, std::string());
          },
          base::Unretained(folders_manager_.get()), item_id));

  context_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(context_menu_delegate_.get());
  context_menu_model_->AddItem(kContextMenuUnpinTabId, u"Unpin Tab");
  if (!folder_id.empty()) {
    context_menu_model_->AddItem(kContextMenuRemoveFromFolderId,
                                 u"Remove from Folder");
  }

  move_to_space_submenu_ = BuildMoveToSpaceSubMenu(
      GetTabSpaceId(contents),
      base::BindRepeating(&AvoraPinnedSectionView::MovePinnedTabToSpace,
                          weak_factory_.GetWeakPtr(), contents));
  if (move_to_space_submenu_) {
    context_menu_model_->AddSubMenu(kContextMenuMoveToSpaceId,
                                    u"Move to Space",
                                    move_to_space_submenu_.get());
  }

  context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  context_menu_model_->AddItem(kContextMenuAddFolderId, u"New Folder");

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(GetWidget(), nullptr,
                                  gfx::Rect(screen_point, gfx::Size()),
                                  views::MenuAnchorPosition::kTopLeft,
                                  ui::mojom::MenuSourceType::kMouse);
}

void AvoraPinnedSectionView::OnPersistentPinnedItemContextMenu(
    const std::string& item_id,
    const gfx::Point& screen_point) {
  if (!pinned_items_manager_ || item_id.empty()) {
    return;
  }

  // No live tab exists for this row, so the item's own Space is the only
  // "current" there is.
  move_to_space_submenu_ = BuildMoveToSpaceSubMenu(
      pinned_items_manager_->GetActiveSpaceId(),
      base::BindRepeating(&AvoraPinnedSectionView::MovePinnedItemToSpace,
                          weak_factory_.GetWeakPtr(), item_id));
  if (!move_to_space_submenu_) {
    return;
  }

  context_menu_delegate_.reset();
  context_menu_model_ = std::make_unique<ui::SimpleMenuModel>(nullptr);
  context_menu_model_->AddSubMenu(kContextMenuMoveToSpaceId,
                                  u"Move to Space",
                                  move_to_space_submenu_.get());

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(GetWidget(), nullptr,
                                  gfx::Rect(screen_point, gfx::Size()),
                                  views::MenuAnchorPosition::kTopLeft,
                                  ui::mojom::MenuSourceType::kMouse);
}

std::unique_ptr<ui::SimpleMenuModel>
AvoraPinnedSectionView::BuildMoveToSpaceSubMenu(
    const std::string& current_space_id,
    base::RepeatingCallback<void(const std::string&)> on_pick) {
  if (!space_manager_) {
    return nullptr;
  }
  auto submenu = std::make_unique<MoveToSpaceSubMenuModel>(
      space_manager_->GetSpaces(), current_space_id, std::move(on_pick));
  // A single-Space install has nowhere to move to; show no entry at all
  // rather than an empty submenu.
  if (!submenu->has_destinations()) {
    return nullptr;
  }
  return submenu;
}

void AvoraPinnedSectionView::MovePinnedTabToSpace(
    content::WebContents* contents,
    const std::string& space_id) {
  if (!tab_strip_model_ || !browser_ || space_id.empty()) {
    return;
  }
  // The menu is asynchronous, so the tab may already be gone by the time a
  // Space is picked.  This only compares pointers, never dereferences the
  // captured one, so a closed tab resolves to kNoTab instead of a use-after-
  // free.
  if (tab_strip_model_->GetIndexOfWebContents(contents) ==
      TabStripModel::kNoTab) {
    return;
  }

  // Move the persisted record first: it is what makes the row survive a
  // restart, so leaving it behind would resurrect the item in the old Space
  // on the next launch.  AvoraSpaceTabFilter does this too when it sees the
  // retag below, and both are idempotent -- this call site keeps its own so
  // the record never depends on a notification arriving.
  if (pinned_items_manager_) {
    const std::string item_id = GetPinnedItemIdForTab(contents);
    if (!item_id.empty()) {
      avora::MovePinnedItemToSpace(browser_->GetProfile()->GetPrefs(), item_id,
                                   GetTabSpaceId(contents), space_id);
    }
  }

  // Retagging the WebContents is the whole move for the live tab.  The
  // change notification is what carries it to AvoraSpaceTabFilter, which
  // owns tab visibility for this window; see AdoptRetaggedTab() there.
  SetTabSpaceId(contents, space_id);
  tab_strip_model_->UpdateWebContentsState(contents, TabChangeType::kAll);
}

void AvoraPinnedSectionView::MovePinnedItemToSpace(
    const std::string& item_id,
    const std::string& space_id) {
  if (!pinned_items_manager_ || !browser_ || space_id.empty()) {
    return;
  }
  // No live tab means no retag, so nothing else will reconcile this item --
  // the shared helper is the whole move here, folder scrub included.
  avora::MovePinnedItemToSpace(browser_->GetProfile()->GetPrefs(), item_id,
                               pinned_items_manager_->GetActiveSpaceId(),
                               space_id);
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
