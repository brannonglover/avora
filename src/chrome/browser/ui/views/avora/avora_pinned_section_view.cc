// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_pinned_section_view.h"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/avora/avora_tab_space.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/avora/avora_import_provenance.h"
#include "chrome/browser/avora/import/arc_sidebar_parser.h"
#include "chrome/browser/avora/import/avora_import_coordinator.h"
#include "chrome/browser/ui/views/avora/avora_import_dialog_view.h"
#include "chrome/browser/ui/views/avora/avora_link_preview_tab_marker.h"
#include "chrome/browser/ui/views/avora/avora_lucide_icon.h"
#include "chrome/browser/ui/views/avora/avora_parked_pin_view.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_materializer.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_tab_marker.h"
#include "chrome/browser/ui/tabs/tab_change_type.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_callback.h"
#include "components/favicon_base/favicon_types.h"
#include "cc/paint/paint_flags.h"
#include "content/public/browser/web_contents.h"
#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/accessibility/ax_enums.mojom.h"
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
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/gfx/shadow_value.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
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
constexpr int kContextMenuImportArcId = 8;

// Command ids for the "Move to Space" submenu, one per destination Space.
// Menus dispatch by command id across the whole hierarchy, so this range has
// to stay clear of the fixed ids above.
constexpr int kContextMenuFirstSpaceId = 100;

// The folder icon takes its colour from the product mark: the periwinkle the
// "A" in the Avora app icon passes through at its crossbar, sampled from
// branding/avora_icon_1024.png.
//
// Note this sits very close to kDragAccentColor below (#6EA8FF), the blue this
// same section draws its insertion line and lifted drag card in -- close
// enough that the two do not distinguish themselves by hue.  What separates
// them in practice is shape and timing: the insertion line is a horizontal
// rule that only exists mid-drag, never alongside a resting folder icon.
constexpr SkColor kFolderIconColor = SkColorSetRGB(0x68, 0xA1, 0xEC);

// The front flap of an open folder, lifted toward white by about a third so
// the two planes separate.  A flat silhouette cannot say "open" on its own at
// 16dip; giving the nearer surface the lighter value is the cheapest cue that
// reads as depth rather than as a second, unrelated shape.
constexpr SkColor kFolderIconFlapColor = SkColorSetRGB(0x95, 0xBD, 0xF2);
constexpr SkColor kRowHoverBg = SkColorSetARGB(0x1A, 0xFF, 0xFF, 0xFF);
constexpr SkColor kLabelColor = SkColorSetARGB(0xE0, 0xED, 0xF2, 0xF5);

// The dragged row, drawn as a card lifted off the list: an inset rounded
// rect with a soft shadow and a thin accent edge, in the same blue the
// section already uses to say "this is where it goes".
constexpr SkColor kDragAccentColor = SkColorSetARGB(0xFF, 0x6E, 0xA8, 0xFF);
constexpr SkColor kDragCardBg = SkColorSetARGB(0x38, 0x9A, 0xC4, 0xFF);
constexpr int kDragCardInsetX = 6;
constexpr int kDragCardInsetY = 2;
constexpr float kDragCardRadius = 8.0f;

// Paints the lifted card for a row being dragged.  Inset from the row's own
// bounds on both axes so the shadow has room to fall inside them: a view's
// paint is clipped to its bounds, and a shadow drawn at the edge would be
// sliced off square.
void PaintLiftedRowBackground(gfx::Canvas* canvas, const gfx::Rect& bounds) {
  gfx::RectF card(bounds);
  card.Inset(gfx::InsetsF::VH(kDragCardInsetY, kDragCardInsetX));

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(kDragCardBg);
  flags.setLooper(gfx::CreateShadowDrawLooper(
      {gfx::ShadowValue(gfx::Vector2d(0, 1), 6,
                        SkColorSetARGB(0x70, 0x00, 0x00, 0x00))}));
  canvas->DrawRoundRect(card, kDragCardRadius, flags);

  cc::PaintFlags edge;
  edge.setAntiAlias(true);
  edge.setStyle(cc::PaintFlags::kStroke_Style);
  edge.setStrokeWidth(1.0f);
  edge.setColor(SkColorSetA(kDragAccentColor, 0x99));
  card.Inset(0.5f);
  canvas->DrawRoundRect(card, kDragCardRadius - 0.5f, edge);
}


// Lucide "corner-up-left": the return arrow the favicon becomes under the
// pointer while its tab is away.
constexpr char kReturnIconPath[] = "M20 20v-7a4 4 0 0 0-4-4H4 M9 14 4 9l5-5";

// Lucide's 2.0 weight thins out at the 16dip favicon size, so the arrow is
// stroked a little heavier -- same reasoning, and same value, as
// AvoraLinkPreviewTabIndicator.
constexpr float kReturnIconStrokeWidth = 2.4f;

// The away state: the favicon lifted off the row.  Close and slightly below,
// the way a small object sits just above a surface -- a deeper shadow would
// read as an alert, which a tab that has wandered off its pin is not.
// Returned by value rather than kept in a static: -Wexit-time-destructors.
gfx::ShadowValues AwayShadowValues() {
  return {gfx::ShadowValue(gfx::Vector2d(0, 1), /*blur=*/4.0f,
                           SkColorSetARGB(0xCC, 0x00, 0x00, 0x00))};
}

// Where the favicon sits inside the shadowed image the values above produce
// (positive insets; ShadowValue::GetMargin is negative because it grows
// outward).
gfx::Insets AwayShadowPadding() {
  return -gfx::ShadowValue::GetMargin(AwayShadowValues());
}

// Lucide "minus": what a pinned row shows where a daily tab shows its "x".
// Not an "x" because it does not get rid of anything the user chose -- the
// pin stays, only its live tab goes.
constexpr char kCloseTabIconPath[] = "M5 12h14";

// Same glyph size as the daily tab's close "x", inside a larger square hit
// target, so the two controls are interchangeable at a glance.
constexpr int kCloseGlyphSize = 14;
constexpr int kCloseButtonSize = 24;

// The minus button on a live pinned row.
class PinnedRowCloseButton : public views::Button {
  METADATA_HEADER(PinnedRowCloseButton, views::Button)

 public:
  explicit PinnedRowCloseButton(PressedCallback callback)
      : views::Button(std::move(callback)) {
    SetPreferredSize(gfx::Size(kCloseButtonSize, kCloseButtonSize));
    SetTooltipText(u"Close Tab");
    GetViewAccessibility().SetName(u"Close Tab");
    // As TabCloseButton: reachable by assistive tech, but not a Tab stop of
    // its own in every row of the sidebar.
    SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (GetState() == views::Button::STATE_HOVERED ||
        GetState() == views::Button::STATE_PRESSED) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(kRowHoverBg);
      flags.setStyle(cc::PaintFlags::kFill_Style);
      canvas->DrawRoundRect(GetLocalBounds(), 6.0f, flags);
    }
    gfx::Rect glyph(GetLocalBounds());
    glyph.ClampToCenteredSize(gfx::Size(kCloseGlyphSize, kCloseGlyphSize));
    PaintLucidePathData(canvas, glyph, kCloseTabIconPath, kLabelColor);
  }
};

BEGIN_METADATA(PinnedRowCloseButton)
END_METADATA

// How many deactivated tabs AvoraPinnedSectionView remembers.  Only the front
// is normally used; the rest cover the tab before it having been closed too.
constexpr size_t kActivationHistoryLimit = 16;

// Paints a simple folder icon at the given position using Skia.  |open| picks
// the glyph for an expanded folder, whose members are listed below it.
//
// The glyph is placed so that its ink -- not the box it is given -- sits
// centred on the row: the tab hangs above the body, so a shape laid out from
// the top of the box reads low against a label the layout has centred.  The
// offsets below put the combined centre of mass of tab and body on the middle
// of the icon area.
//
// Both states span the same ink bounds vertically (0.08s to 0.88s), so
// toggling a folder swaps the shape without the row appearing to shift.
void PaintFolderIcon(gfx::Canvas* canvas, int x, int y, int size, bool open) {
  const float s = static_cast<float>(size);
  const float fx = static_cast<float>(x);
  const float fy = static_cast<float>(y);
  const float r = s * 0.1f;

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(kFolderIconColor);

  // Folder tab: small rounded rect at top-left.  Shared by both states -- it
  // is the part of the folder that does not move when one is opened.
  SkRect tab = SkRect::MakeXYWH(fx, fy + s * 0.08f, s * 0.42f, s * 0.20f);

  if (!open) {
    // Closed: the body is a single rounded rect behind the tab.
    SkRect body = SkRect::MakeXYWH(fx, fy + s * 0.22f, s, s * 0.66f);
    canvas->DrawRoundRect(gfx::SkRectToRectF(body), r, flags);
    canvas->DrawRoundRect(gfx::SkRectToRectF(tab), r, flags);
    return;
  }

  // Open: the back panel stays upright behind the tab but is drawn narrower
  // and shorter than the closed body, so its top-left corner clears the
  // front flap and stays visible.
  SkRect back = SkRect::MakeXYWH(fx, fy + s * 0.22f, s * 0.84f, s * 0.58f);
  canvas->DrawRoundRect(gfx::SkRectToRectF(back), r, flags);
  canvas->DrawRoundRect(gfx::SkRectToRectF(tab), r, flags);

  // The front flap, swung out to the right: a parallelogram sharing the
  // body's bottom edge, leaning so its top edge overhangs the back panel.
  // Drawn last and in the lighter value so it sits in front of the panel
  // rather than merging with it.
  flags.setColor(kFolderIconFlapColor);
  const SkPath front = SkPathBuilder()
                           .moveTo(fx + s * 0.12f, fy + s * 0.88f)
                           .lineTo(fx + s * 0.30f, fy + s * 0.44f)
                           .lineTo(fx + s * 1.00f, fy + s * 0.44f)
                           .lineTo(fx + s * 0.82f, fy + s * 0.88f)
                           .close()
                           .detach();
  canvas->DrawPath(front, flags);
}

// Context menu delegate for the section-level "Add Folder" action.
class SectionContextMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  SectionContextMenuDelegate(base::OnceClosure on_add_folder,
                             base::OnceClosure on_import,
                             base::OnceClosure on_import_arc)
      : on_add_folder_(std::move(on_add_folder)),
        on_import_(std::move(on_import)),
        on_import_arc_(std::move(on_import_arc)) {}

  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == kContextMenuAddFolderId && on_add_folder_) {
      std::move(on_add_folder_).Run();
    } else if (command_id == kContextMenuImportBookmarksId && on_import_) {
      std::move(on_import_).Run();
    } else if (command_id == kContextMenuImportArcId && on_import_arc_) {
      std::move(on_import_arc_).Run();
    }
  }

 private:
  base::OnceClosure on_add_folder_;
  base::OnceClosure on_import_;
  base::OnceClosure on_import_arc_;
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
      expanded_(expanded),
      on_toggle_(std::move(on_toggle)),
      on_context_(std::move(on_context)),
      on_rename_(std::move(on_rename)) {
  SetPreferredSize(gfx::Size(0, AvoraPinnedSectionView::kRowHeight));
  // A header drags as the folder it heads; it has no pinned item of its own.
  SetDragContext(/*item_id=*/std::string(), folder_id);

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
  if (is_dragged()) {
    PaintLiftedRowBackground(canvas, GetLocalBounds());
    // The folder icon still has to be drawn; the highlight and separator
    // below are what the lifted card replaces.
    if (icon_spacer_) {
      const gfx::Rect r = icon_spacer_->bounds();
      PaintFolderIcon(canvas, r.x(), r.y(),
                      AvoraPinnedSectionView::kFaviconSize, expanded_);
    }
    return;
  }
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
                    AvoraPinnedSectionView::kFaviconSize, expanded_);
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
  if (event.IsLeftMouseButton() && event.GetClickCount() == 2) {
    BeginRename();
    return true;
  }
  // A single left press may still turn out to be a drag of the whole folder,
  // so the base class holds it until release; see PinnedRowView.
  return PinnedRowView::OnMousePressed(event);
}

void FolderHeaderView::PerformClickAction() {
  if (!on_toggle_) {
    return;
  }
  // Toggling rebuilds the section and deletes this header: copy the callback
  // and the id it takes, and touch no member afterwards.
  auto on_toggle = on_toggle_;
  const std::string folder_id = folder_id_;
  on_toggle.Run(folder_id);
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
// PinnedRowView
// ---------------------------------------------------------------------------

namespace {

// Vertical travel before a press on a row becomes a reorder drag.  Below it
// the press is still a click, so an unsteady hand cannot turn activating a
// pin into moving it.
constexpr int kRowDragThreshold = 5;

// How far outside the section the pointer may stray and still count as a
// drop.  Past it the drag is parked (no indicator, release does nothing),
// since the user is on their way to the favorites grid or the daily list.
constexpr int kRowDragSlop = 8;

}  // namespace

PinnedRowView::PinnedRowView() = default;

PinnedRowView::~PinnedRowView() = default;

void PinnedRowView::SetDragContext(const std::string& item_id,
                                   const std::string& folder_id) {
  drag_item_id_ = item_id;
  drag_folder_id_ = folder_id;
}

void PinnedRowView::SetRowLayout(views::BoxLayout* layout, bool in_folder) {
  row_layout_ = layout;
  in_folder_ = in_folder;
}

void PinnedRowView::SetRowIndent(bool in_folder) {
  if (!row_layout_ || in_folder_ == in_folder) {
    return;
  }
  in_folder_ = in_folder;
  row_layout_->set_inside_border_insets(gfx::Insets::VH(
      0, AvoraPinnedSectionView::kHPadding +
             (in_folder ? AvoraPinnedSectionView::kIndent : 0)));
  InvalidateLayout();
}

void PinnedRowView::SetDragged(bool dragged) {
  if (is_dragged_ == dragged) {
    return;
  }
  is_dragged_ = dragged;
  SchedulePaint();
}

AvoraPinnedSectionView* PinnedRowView::GetSection() {
  return parent() ? views::AsViewClass<AvoraPinnedSectionView>(parent())
                  : nullptr;
}

bool PinnedRowView::OnMousePressed(const ui::MouseEvent& event) {
  // Whatever the subclass wanted from this press it has already taken; only
  // a plain left press reaches here, and it stays unresolved until the mouse
  // either moves far enough to be a drag or comes back up as a click.
  if (!event.IsLeftMouseButton()) {
    return false;
  }
  press_point_ = event.location();
  press_pending_ = true;
  dragging_ = false;
  return true;
}

bool PinnedRowView::OnMouseDragged(const ui::MouseEvent& event) {
  if (!press_pending_) {
    return false;
  }
  if (!dragging_) {
    if (std::abs(event.location().y() - press_point_.y()) <
        kRowDragThreshold) {
      return true;
    }
    AvoraPinnedSectionView* section = GetSection();
    if (!section || !section->BeginRowDrag(this)) {
      // Nothing to reorder (see drag_item_id_): swallow the gesture rather
      // than let it fall back to activating the row on release.
      press_pending_ = false;
      return true;
    }
    dragging_ = true;
  }

  gfx::Point screen_point = event.location();
  ConvertPointToScreen(this, &screen_point);
  if (AvoraPinnedSectionView* section = GetSection()) {
    section->UpdateRowDrag(screen_point);
  }
  return true;
}

void PinnedRowView::OnMouseReleased(const ui::MouseEvent& event) {
  const bool was_dragging = dragging_;
  const bool was_click = press_pending_ && !dragging_;
  press_pending_ = false;
  dragging_ = false;

  if (was_dragging) {
    if (AvoraPinnedSectionView* section = GetSection()) {
      section->EndRowDrag(/*commit=*/true);
    }
    return;
  }
  // A press that never moved: activate, exactly as pressing used to.  This
  // deletes the row -- touch nothing afterwards.
  if (was_click && event.IsLeftMouseButton() &&
      GetLocalBounds().Contains(event.location())) {
    PerformClickAction();
  }
}

void PinnedRowView::OnMouseCaptureLost() {
  const bool was_dragging = dragging_;
  press_pending_ = false;
  dragging_ = false;
  if (was_dragging) {
    if (AvoraPinnedSectionView* section = GetSection()) {
      section->EndRowDrag(/*commit=*/false);
    }
  }
}

BEGIN_METADATA(PinnedRowView)
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
  SetRowLayout(layout, /*in_folder=*/!folder_id.empty());

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
  if (is_dragged()) {
    PaintLiftedRowBackground(canvas, GetLocalBounds());
    return;
  }
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
  // Both handlers here can destroy this row before they return: the context
  // handler opens a menu and the click handler materializes a tab -- each
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
  // A left press may still turn out to be a drag, so the base class holds it
  // until release; see PinnedRowView.
  return PinnedRowView::OnMousePressed(event);
}

void FolderTabRow::PerformClickAction() {
  if (!on_click_) {
    return;
  }
  auto on_click = on_click_;
  on_click.Run(item_id_);
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
// PinnedRowIcon
// ---------------------------------------------------------------------------

// static
int PinnedRowIcon::ShadowInset() {
  // The widest side of the shadow, applied to all four: the favicon stays
  // centred, so the box has to hold the deepest edge (below it) on every
  // side.
  const gfx::Insets padding = AwayShadowPadding();
  return std::max({padding.left(), padding.right(), padding.top(),
                   padding.bottom()});
}

PinnedRowIcon::PinnedRowIcon(base::RepeatingClosure on_return)
    : on_return_(std::move(on_return)) {
  const int extent =
      AvoraPinnedSectionView::kFaviconSize + 2 * ShadowInset();
  SetPreferredSize(gfx::Size(extent, extent));
  // Nothing to announce while the tab is on its target: the row's own title
  // already names it, and a second node for the picture beside that title
  // would only be noise.
  GetViewAccessibility().SetIsIgnored(true);
}

PinnedRowIcon::~PinnedRowIcon() = default;

void PinnedRowIcon::SetFavicon(const gfx::ImageSkia& icon) {
  icon_ = icon;
  // Built here rather than in OnPaint: the shadow is a property of the
  // favicon, not of the frame being drawn.  (ImageSkia is lazy, so this
  // costs nothing until the row is actually away and painted.)
  shadowed_icon_ = icon.isNull()
                       ? gfx::ImageSkia()
                       : gfx::ImageSkiaOperations::CreateImageWithDropShadow(
                             icon, AwayShadowValues());
  SchedulePaint();
}

void PinnedRowIcon::SetAwayFromTarget(bool away) {
  if (away_ == away) {
    return;
  }
  away_ = away;
  SchedulePaint();
}

void PinnedRowIcon::OnPaint(gfx::Canvas* canvas) {
  // The favicon's own 16dip box, centred in the larger box the shadow needs.
  // Everything is positioned off this, so the favicon does not move between
  // the two states -- only its shadow appears.
  const gfx::Rect favicon_bounds(
      (width() - AvoraPinnedSectionView::kFaviconSize) / 2,
      (height() - AvoraPinnedSectionView::kFaviconSize) / 2,
      AvoraPinnedSectionView::kFaviconSize,
      AvoraPinnedSectionView::kFaviconSize);

  // Hovering the lifted favicon is the user asking what it means, so the
  // answer is the action itself: it becomes the arrow that undoes the
  // wandering.
  if (away_ && IsMouseHovered()) {
    PaintLucidePathData(canvas, favicon_bounds, kReturnIconPath, kLabelColor,
                        kReturnIconStrokeWidth);
    return;
  }

  if (away_ && !shadowed_icon_.isNull()) {
    // The shadowed image carries the favicon at its own padding offset;
    // backing that out is what lands the favicon itself on |favicon_bounds|.
    const gfx::Insets padding = AwayShadowPadding();
    canvas->DrawImageInt(shadowed_icon_, favicon_bounds.x() - padding.left(),
                         favicon_bounds.y() - padding.top());
    return;
  }

  if (!icon_.isNull()) {
    canvas->DrawImageInt(icon_, favicon_bounds.x(), favicon_bounds.y());
  }
}

bool PinnedRowIcon::OnMousePressed(const ui::MouseEvent& event) {
  // Every other click belongs to the row: returning false here is what keeps
  // activation, rename and the context menu working when the pointer happens
  // to be over the favicon rather than the title.
  if (!away_ || !event.IsLeftMouseButton() || !on_return_) {
    return false;
  }
  // Reentrant, like every other click in this section: returning navigates
  // and activates, which rebuilds the section and destroys this view before
  // Run() comes back.  Nothing may be touched afterwards.
  on_return_.Run();
  return true;
}

void PinnedRowIcon::OnMouseEntered(const ui::MouseEvent& event) {
  if (away_) {
    SchedulePaint();
  }
}

void PinnedRowIcon::OnMouseExited(const ui::MouseEvent& event) {
  if (away_) {
    SchedulePaint();
  }
}

views::View* PinnedRowIcon::GetTooltipHandlerForPoint(const gfx::Point& point) {
  // Only the marker has anything of its own to say (where clicking goes).
  // Otherwise the row keeps showing its title, as it does everywhere else
  // along its width.
  return away_ ? this : parent();
}

BEGIN_METADATA(PinnedRowIcon)
END_METADATA

// ---------------------------------------------------------------------------
// PinnedTabRow
// ---------------------------------------------------------------------------

PinnedTabRow::PinnedTabRow(
    content::WebContents* contents,
    base::RepeatingCallback<void(content::WebContents*)> on_click,
    base::RepeatingCallback<void(content::WebContents*,
                                 const gfx::Point&)> on_context,
    PinnedTabRow::RenameCallback on_rename,
    base::RepeatingCallback<void(content::WebContents*)> on_return,
    base::RepeatingCallback<void(content::WebContents*)> on_close,
    bool in_folder)
    : contents_(contents),
      on_click_(std::move(on_click)),
      on_context_(std::move(on_context)),
      on_rename_(std::move(on_rename)),
      on_return_(std::move(on_return)),
      on_close_(std::move(on_close)) {
  SetPreferredSize(gfx::Size(0, AvoraPinnedSectionView::kRowHeight));

  // Same leading inset FolderTabRow computes for a folder member, so the two
  // row types are interchangeable at a given depth.
  // The favicon slot is wider than the favicon (PinnedRowIcon holds its
  // shadow), so the leading padding and the icon-to-title gap each give that
  // difference back.  Without it every live row's favicon and title would
  // sit a few pixels right of the unmaterialized rows beside them.
  // The trailing padding gives back the minus button's hit-target margin the
  // same way, so the glyph itself sits kHPadding in from the edge.
  const int shadow_inset = PinnedRowIcon::ShadowInset();
  const int close_inset = (kCloseButtonSize - kCloseGlyphSize) / 2;
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(0,
                        AvoraPinnedSectionView::kHPadding - shadow_inset +
                            (in_folder ? AvoraPinnedSectionView::kIndent : 0),
                        0, AvoraPinnedSectionView::kHPadding - close_inset),
      8 - shadow_inset));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  SetRowLayout(layout, in_folder);

  // Favicon — 16px, and the row's away marker and return control besides
  // (see PinnedRowIcon).  Inert until SetPinnedTarget() says otherwise.
  favicon_view_ = AddChildView(std::make_unique<PinnedRowIcon>(
      base::BindRepeating(&PinnedTabRow::ReturnToTarget,
                          base::Unretained(this))));

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

  // Minus (hidden until hovered).  A press on it is the button's own, so it
  // never reaches PinnedRowView as the start of an activation or a drag.
  if (on_close_) {
    close_button_ = AddChildView(std::make_unique<PinnedRowCloseButton>(
        base::BindRepeating(&PinnedTabRow::CloseTab, base::Unretained(this))));
    close_button_->SetVisible(false);
  }

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
  favicon_view_->SetFavicon(resized);
}

void PinnedTabRow::SetPinnedTarget(const GURL& target, bool away) {
  target_url_ = target;
  if (!favicon_view_) {
    return;
  }
  favicon_view_->SetAwayFromTarget(away);

  // The host, not a sentence: hovering the marker should answer "where does
  // this take me", which is the one thing the icon cannot draw.
  favicon_view_->SetTooltipText(away ? base::UTF8ToUTF16(target.host())
                                     : std::u16string());
  favicon_view_->GetViewAccessibility().SetIsIgnored(!away);
  if (away) {
    favicon_view_->GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
    favicon_view_->GetViewAccessibility().SetName(
        u"Return to " + base::UTF8ToUTF16(target.host()));
  }
}

void PinnedTabRow::ReturnToTarget() {
  if (on_return_ && contents_) {
    // Destroys this row (the navigation and activation rebuild the section),
    // so nothing may follow.
    on_return_.Run(contents_);
  }
}

void PinnedTabRow::CloseTab() {
  if (on_close_ && contents_) {
    // Destroys this row (the tab's removal rebuilds the section), so copy
    // what the call needs and touch nothing afterwards.
    auto on_close = on_close_;
    on_close.Run(contents_);
  }
}

void PinnedTabRow::UpdateCloseButtonVisibility() {
  if (!close_button_) {
    return;
  }
  const bool visible = IsMouseHovered() && !is_renaming_;
  if (close_button_->GetVisible() != visible) {
    close_button_->SetVisible(visible);
    InvalidateLayout();
  }
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
  if (is_dragged()) {
    PaintLiftedRowBackground(canvas, GetLocalBounds());
    return;
  }
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
  UpdateCloseButtonVisibility();
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
  UpdateCloseButtonVisibility();
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
  UpdateCloseButtonVisibility();
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
  if (event.IsLeftMouseButton() && event.GetClickCount() == 2) {
    StartRename();
    return true;
  }
  // A single left press may still turn out to be a drag, so the base class
  // holds it until release; see PinnedRowView.
  return PinnedRowView::OnMousePressed(event);
}

void PinnedTabRow::PerformClickAction() {
  if (!on_click_) {
    return;
  }
  // Activating rebuilds the section and deletes this row: copy the callback
  // and touch no member afterwards.
  auto on_click = on_click_;
  on_click.Run(contents_);
}

void PinnedTabRow::OnMouseEntered(const ui::MouseEvent& event) {
  UpdateCloseButtonVisibility();
  SchedulePaint();
}

void PinnedTabRow::OnMouseExited(const ui::MouseEvent& event) {
  UpdateCloseButtonVisibility();
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
  if (selection.active_tab_changed() && selection.old_tab) {
    RememberDeactivatedTab(selection.old_tab);
  }
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
                     weak_factory_.GetWeakPtr()),
      base::BindOnce(&AvoraPinnedSectionView::OnImportArcClicked,
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
  // Arc's Spaces and pinned tabs are a separate import from bookmarks: they
  // become real Avora Spaces and pinned items rather than entries in the
  // Imported staging section.
  context_menu_model_->AddItem(kContextMenuImportArcId,
                               u"Import Spaces & Pins from Arc…");

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(GetWidget(), nullptr,
                                  gfx::Rect(point, gfx::Size()),
                                  views::MenuAnchorPosition::kTopLeft,
                                  source_type);
}

void AvoraPinnedSectionView::OnImportArcClicked() {
  if (!browser_) {
    return;
  }
  Profile* profile = Profile::FromBrowserContext(browser_->GetProfile());
  if (!profile || !profile->GetPrefs()) {
    return;
  }
  PrefService* prefs = profile->GetPrefs();

  // Deliberately fresh manager instances rather than this view's: the import
  // re-scopes them per Space via SetWindowActiveSpaceId, and doing that to the
  // view's own managers would silently move this window to whichever Space was
  // imported last.  These are pref-backed and self-syncing, so the sidebar
  // picks the new Spaces and pins up through the usual observers.
  SpaceManager space_manager(prefs);
  PinnedItemsManager pinned_items(prefs);
  PinnedFoldersManager pinned_folders(prefs);
  ImportProvenanceStore provenance(prefs);

  const ArcImportResult result = AvoraImportCoordinator::ImportArcSidebar(
      ArcSidebarParser::DefaultSidebarPath(), &space_manager, &pinned_items,
      &pinned_folders, &provenance);

  if (!result.success) {
    LOG(ERROR) << "Avora: Arc import failed: " << result.error_message;
    return;
  }
  LOG(INFO) << "Avora: Arc import -- spaces created "
            << result.spaces_created << ", updated " << result.spaces_updated
            << "; pinned items created " << result.items_created
            << ", merged " << result.items_updated << "; folders created "
            << result.folders_created << ", merged "
            << result.folders_updated << "; skipped "
            << result.stats.items_skipped;
}

void AvoraPinnedSectionView::OnImportBookmarksClicked() {
  if (browser_) {
    AvoraImportDialogView::Show(browser_, window_space_state_);
  }
}

gfx::Size AvoraPinnedSectionView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  gfx::Size size = views::View::CalculatePreferredSize(available_size);

  // Always leave an empty strip below the last row.  The section context menu
  // (New Folder, and the importers) is only reachable by right-clicking part
  // of the section that is not a row -- a right-click on a row gets the folder
  // or item menu instead.  Without this strip the rows fill the section
  // exactly as soon as anything is pinned, which makes the section menu
  // unreachable for precisely the users who have the most pinned.  The strip
  // doubles as drop space for pinning a dragged tab at the end.
  constexpr int kSectionContextStrip = 16;
  size.set_height(std::max(size.height() + kSectionContextStrip, kRowHeight));
  return size;
}

void AvoraPinnedSectionView::OnPaintBackground(gfx::Canvas* canvas) {
  if (!is_drop_highlighted_) {
    return;
  }

  // A stand-in row is already sitting in the landing spot, and it says where
  // the tab goes more exactly than a line can.  Drawing the line as well
  // states the same thing twice.
  if (has_external_row_drag()) {
    return;
  }

  constexpr SkColor kLineColor = SkColorSetARGB(0xFF, 0x6E, 0xA8, 0xFF);
  constexpr int kLineThickness = 2;

  // Where the tab would land: the tracked insertion point while an external
  // drag is over the rows, and otherwise the section's bottom edge, which is
  // the plain "pin it at the end" answer.
  gfx::Rect bounds = GetLocalBounds();
  int line_y = external_drop_line_y_ >= 0
                   ? std::clamp(external_drop_line_y_ - kLineThickness / 2, 0,
                                bounds.bottom() - kLineThickness)
                   : bounds.bottom() - kLineThickness;
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
  if (row_drag_active_) {
    // The user is holding one of these rows; deleting it now would drop the
    // drag mid-gesture.  Fold the change into the rebuild that follows the
    // drop (FinishRowDrag).
    rebuild_pending_ = true;
    return;
  }

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

  // A pinned row's user-chosen name is stored per pinned item (see
  // OnPinnedTabRenamed), not on the item record itself, so an unmaterialized
  // row has to consult it too -- otherwise the same pin shows the name the
  // user gave it while a live tab exists and reverts to the item's original
  // title once that tab is gone.
  auto display_title = [this](const std::string& item_id,
                              const std::string& url,
                              const std::string& title) {
    const std::string custom =
        folders_manager_->GetCustomTitleForItem(item_id, url);
    return custom.empty() ? title : custom;
  };

  // Builds the row for a live pinned tab.  Shared by every section below, so
  // a folder member and a top-level pin are the same row built the same way,
  // differing only in indent and in the folder their drag context names.
  auto add_live_row = [&](content::WebContents* contents,
                          const std::string& item_id,
                          const std::string& folder_id) {
    auto* row = AddChildView(std::make_unique<PinnedTabRow>(
        contents,
        base::BindRepeating(&AvoraPinnedSectionView::OnPinnedTabClicked,
                            base::Unretained(this)),
        base::BindRepeating(&AvoraPinnedSectionView::OnPinnedTabContextMenu,
                            base::Unretained(this)),
        base::BindRepeating(&AvoraPinnedSectionView::OnPinnedTabRenamed,
                            base::Unretained(this)),
        base::BindRepeating(&AvoraPinnedSectionView::OnPinnedTabReturnToTarget,
                            base::Unretained(this)),
        base::BindRepeating(&AvoraPinnedSectionView::OnPinnedTabCloseRequested,
                            base::Unretained(this)),
        /*in_folder=*/!folder_id.empty()));
    row->SetDragContext(item_id, folder_id);
    // Before the favicon load below: it is the target, when there is one,
    // that the favicon is loaded for.
    ApplyPinnedTargetState(row);
    // Use custom title if one was saved.  Looked up by the pin, falling back
    // to the pin's *target* -- never to where the tab happens to be, which
    // is a key two different pins can share (see OnPinnedTabRenamed).
    const std::string url = row->target_url().is_valid()
                                ? row->target_url().spec()
                                : contents->GetLastCommittedURL().spec();
    const std::string custom =
        folders_manager_->GetCustomTitleForItem(item_id, url);
    if (!custom.empty()) {
      row->SetCustomTitle(base::UTF8ToUTF16(custom));
    }
    LoadPinnedTabFavicon(row);
  };

  // 1. Which tabs in this window/Space this section speaks for: every tab
  //    that is either natively pinned (TabStripModel::IsTabPinned, i.e.
  //    within IndexOfFirstNonPinnedTab()) or materialized from a persisted
  //    kPinned item (IsPinnedItemTab), or both (a natively pinned tab that a
  //    kPinned item was backfilled onto).  Each gets exactly one
  //    PinnedTabRow -- one intentional sidebar representation per tab, never
  //    two -- but where that row goes is decided in section 1b, not here.
  //
  //    ApplyVisibility() (avora_space_tab_filter.cc) is the other half of
  //    this: it hides a materialized-but-not-natively-pinned tab's row from
  //    the daily/unpinned tab strip, the same way it already hides a
  //    Favorite's tab there, so the tab doesn't ALSO show as a normal daily
  //    row while it has a live PinnedTabRow here. A natively pinned tab's
  //    own strip row is untouched by that rule regardless of kPinned
  //    backing -- only this section's representation changes for it.
  std::vector<std::pair<std::string, content::WebContents*>> live_items;
  std::vector<content::WebContents*> unbacked_live;
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
      if (pinned_item_id.empty()) {
        unbacked_live.push_back(contents);
        continue;
      }
      if (item_folder_id.count(pinned_item_id)) {
        continue;  // Renders under its folder in section 2 instead.
      }
      live_items.emplace_back(pinned_item_id, contents);
    }
  }

  // 1a. Natively pinned tabs that never got a kPinned record: nothing
  //     persisted to order them by, so they lead the section and, having no
  //     item id in their drag context, cannot be dragged to reorder.
  for (content::WebContents* contents : unbacked_live) {
    add_live_row(contents, /*item_id=*/std::string(),
                 /*folder_id=*/std::string());
  }

  // 1b/2. The top-level sequence: pinned items in the order the Space
  //       persists them, with each folder drawn after the item it is
  //       anchored to (PinnedFolder::after_item_id).  Items and folders
  //       interleave in one list, so a pin can sit below a folder as easily
  //       as above one -- there is no "pins block, then folders block".
  //
  //       An item renders live where a tab already materializes it,
  //       otherwise as a row that materializes one lazily on click
  //       (OnPersistentPinnedItemClicked); nothing here ever materializes
  //       eagerly.  Live and unmaterialized are two states of one item, so
  //       both come from this single walk of the persisted list rather than
  //       live rows first: otherwise a row would jump to the top of the
  //       section the moment it got a tab, and a drag that reordered it
  //       would snap back on the next rebuild.  Folder members are excluded
  //       here; they render under their folder.
  std::map<std::string, content::WebContents*> live_by_item;
  for (const auto& [item_id, contents] : live_items) {
    live_by_item.emplace(item_id, contents);
  }
  std::set<content::WebContents*> rendered_live;

  auto add_item_row = [&](const PinnedItemEntry& item) {
    auto live = live_by_item.find(item.id);
    if (live != live_by_item.end()) {
      rendered_live.insert(live->second);
      add_live_row(live->second, item.id, /*folder_id=*/std::string());
      return;
    }
    auto* row = AddChildView(std::make_unique<FolderTabRow>(
        /*folder_id=*/std::string(), item.id, item.url,
        display_title(item.id, item.url, item.title),
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
    row->SetDragContext(item.id, /*folder_id=*/std::string());
    LoadFavicon(row);
  };

  // A folder: its header, then its members in ordered_item_ids order.  A
  // member uses the exact same materialize/activate/context-menu handlers as
  // a top-level item -- there is no folder-specific tab-opening path.
  auto add_folder = [&](const PinnedFolder& folder) {
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
      return;
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
        add_live_row(tab_strip_model_->GetWebContentsAt(live_index), item_id,
                     folder.id);
        continue;
      }

      auto* row = AddChildView(std::make_unique<FolderTabRow>(
          folder.id, item_id, entry->url,
          display_title(item_id, entry->url, entry->title),
          base::BindRepeating(
              &AvoraPinnedSectionView::OnPersistentPinnedItemClicked,
              base::Unretained(this)),
          base::BindRepeating(&AvoraPinnedSectionView::OnFolderItemContextMenu,
                              base::Unretained(this))));
      row->SetDragContext(item_id, folder.id);
      LoadFavicon(row);
    }
  };

  std::vector<PinnedItemEntry> top_items;
  if (pinned_items_manager_) {
    for (const auto& item : pinned_items_manager_->GetPinnedItems()) {
      if (!item_folder_id.count(item.id)) {
        top_items.push_back(item);
      }
    }
  }

  // Folders grouped by the item they follow.  An anchor naming an item that
  // is no longer a top-level pin -- unpinned, or since filed into a folder --
  // resolves to the default, after everything, since the place it named does
  // not exist to be drawn at any more.
  std::set<std::string> top_item_ids;
  for (const auto& item : top_items) {
    top_item_ids.insert(item.id);
  }
  std::vector<const PinnedFolder*> before_all_items;
  std::map<std::string, std::vector<const PinnedFolder*>> folders_after_item;
  std::vector<const PinnedFolder*> after_all_items;
  for (const auto& folder : folders) {
    if (!folder.after_item_id) {
      after_all_items.push_back(&folder);
    } else if (folder.after_item_id->empty()) {
      before_all_items.push_back(&folder);
    } else if (top_item_ids.count(*folder.after_item_id)) {
      folders_after_item[*folder.after_item_id].push_back(&folder);
    } else {
      after_all_items.push_back(&folder);
    }
  }

  for (const PinnedFolder* folder : before_all_items) {
    add_folder(*folder);
  }
  for (const auto& item : top_items) {
    add_item_row(item);
    auto after = folders_after_item.find(item.id);
    if (after != folders_after_item.end()) {
      for (const PinnedFolder* folder : after->second) {
        add_folder(*folder);
      }
    }
  }
  for (const PinnedFolder* folder : after_all_items) {
    add_folder(*folder);
  }

  // 1c. Live tabs whose item is no longer in the Space's list at all --
  //     defensive: the walk above goes by the list, so without this such a
  //     tab would stop being drawn anywhere.
  for (const auto& [item_id, contents] : live_items) {
    if (!rendered_live.count(contents)) {
      add_live_row(contents, item_id, /*folder_id=*/std::string());
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

void AvoraPinnedSectionView::SetExternalDropPoint(
    const gfx::Point& screen_point) {
  gfx::Point local = screen_point;
  views::View::ConvertPointFromScreen(this, &local);

  const DropPosition position = ComputeDropPosition(local.y());

  int line_y = -1;
  if (position.onto_folder_header) {
    // Dropping here files the tab into the folder rather than between two
    // rows, and the highlighted header says exactly that.  A line as well
    // would promise an insertion point the drop does not honour.
    HighlightFolder(position.target.folder_id);
  } else {
    ClearHighlight();
    if (position.before_view) {
      line_y = position.before_view->bounds().y();
    } else {
      // After everything: sit on the last row's bottom edge, falling back to
      // the section's own bottom when there are no rows to land after.
      const std::vector<RowSlot> slots = BuildRowSlots();
      line_y = slots.empty() ? GetLocalBounds().bottom()
                             : slots.back().view->bounds().bottom();
    }
  }

  if (external_drop_line_y_ != line_y) {
    external_drop_line_y_ = line_y;
    SchedulePaint();
  }
}

void AvoraPinnedSectionView::ClearExternalDrop() {
  ClearHighlight();
  if (external_drop_line_y_ != -1) {
    external_drop_line_y_ = -1;
    SchedulePaint();
  }
}

bool AvoraPinnedSectionView::BeginExternalRowDrag(
    content::WebContents* contents) {
  if (row_drag_active_ || !contents || !folders_manager_ ||
      !pinned_items_manager_) {
    return false;
  }

  // The same row class Rebuild() draws for a real pin, built from the tab
  // being dragged, so what the preview shows is what the section will hold.
  // Its callbacks go nowhere: this row exists only to be moved, and it is
  // removed before anything could click it.  No close callback, so it has no
  // minus to show either.
  auto* row = AddChildView(std::make_unique<PinnedTabRow>(
      contents, base::DoNothing(), base::DoNothing(), base::DoNothing(),
      base::DoNothing(),
      base::RepeatingCallback<void(content::WebContents*)>()));

  // Distinct from any real PinnedItem id, which are generated tokens, so a
  // stand-in can never be mistaken for something persisted.
  external_provisional_id_ = "__avora_external_row_drag__";
  row->SetDragContext(external_provisional_id_, std::string());
  row->SetDragged(true);
  // No pinned record exists yet, so there is no target to prefer; the loader
  // falls back to the tab's own URL, which is the right identity here.
  LoadPinnedTabFavicon(row);

  row_drag_active_ = true;
  row_drag_views_.clear();
  row_drag_views_.push_back(row);
  row_drag_item_id_ = external_provisional_id_;
  row_drag_folder_id_.clear();
  // It came from outside, so there is no slot here to put it back into; a
  // drag that wanders out of the section previews at the end instead.
  row_drag_origin_before_ = nullptr;
  row_drop_valid_ = false;
  row_drop_onto_folder_ = false;
  InvalidateLayout();
  // The line stops being drawn from here; repaint so it goes with the same
  // frame the stand-in arrives in rather than the next one.
  SchedulePaint();
  return true;
}

void AvoraPinnedSectionView::UpdateExternalRowDrag(
    const gfx::Point& screen_point) {
  if (!has_external_row_drag()) {
    return;
  }
  // Identical to an in-section drag from here on, which is the whole point.
  UpdateRowDrag(screen_point);
}

void AvoraPinnedSectionView::SetExternalRowDragInSection(bool in_section) {
  if (!has_external_row_drag() || row_drag_views_.empty()) {
    return;
  }
  views::View* row = row_drag_views_.front();
  if (!row || row->GetVisible() == in_section) {
    return;
  }

  row->SetVisible(in_section);
  if (!in_section) {
    // Nothing here is being aimed at any more, so a release out there must
    // not commit a position this section stopped showing.
    row_drop_valid_ = false;
    row_drop_onto_folder_ = false;
    ClearHighlight();
  }
  // BuildRowSlots() and BuildTopLevelSequence() both skip invisible children,
  // so the hidden row drops out of the drop maths as well as out of sight.
  InvalidateLayout();
  SchedulePaint();
}

void AvoraPinnedSectionView::EndExternalRowDrag(bool commit,
                                                const std::string& item_id) {
  if (!has_external_row_drag()) {
    return;
  }

  const bool apply = commit && row_drop_valid_ && !item_id.empty();
  const DropTarget target = row_drop_target_;
  // Captured while the rows still stand where the preview put them, the same
  // moment EndRowDrag() captures it.
  std::vector<TopLevelEntry> sequence =
      apply ? BuildTopLevelSequence() : std::vector<TopLevelEntry>();
  // The stand-in held the place under its provisional id; the item the caller
  // just created is what actually has to be written there.
  for (auto& entry : sequence) {
    if (!entry.is_folder && entry.id == external_provisional_id_) {
      entry.id = item_id;
    }
  }

  // Drop the raw_ptrs before deleting what they point at.
  std::vector<views::View*> to_remove;
  for (const auto& dragged : row_drag_views_) {
    to_remove.push_back(dragged);
  }
  row_drag_views_.clear();
  for (views::View* view : to_remove) {
    RemoveChildViewT(view);
  }

  row_drag_origin_before_ = nullptr;
  row_drag_active_ = false;
  row_drag_item_id_.clear();
  row_drag_folder_id_.clear();
  row_drop_valid_ = false;
  row_drop_onto_folder_ = false;
  external_provisional_id_.clear();
  ClearExternalDrop();
  SchedulePaint();

  // Same deferral as EndRowDrag(): writing the order rebuilds every row, and
  // we are still inside the drag's event.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&AvoraPinnedSectionView::FinishRowDrag,
                     weak_factory_.GetWeakPtr(), apply, item_id, target,
                     sequence));
}

void AvoraPinnedSectionView::SetDragActive(bool active) {
  if (is_drag_active_ == active) {
    return;
  }
  is_drag_active_ = active;
  if (!active) {
    // A stand-in row left over from a drag that ended somewhere else -- back
    // in the daily list, on the favorites grid, cancelled -- has to go, and
    // cancelling it is what puts the rows back where they were.
    EndExternalRowDrag(/*commit=*/false, std::string());
    // Also drops the insertion line: the drag is over, so there is no longer
    // a landing spot to promise.
    ClearExternalDrop();
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

std::vector<AvoraPinnedSectionView::RowSlot>
AvoraPinnedSectionView::BuildRowSlots() const {
  std::vector<RowSlot> slots;
  for (views::View* child : children()) {
    if (!child->GetVisible() || IsDraggedView(child)) {
      continue;
    }
    if (auto* header = views::AsViewClass<FolderHeaderView>(child)) {
      slots.push_back({child, /*is_header=*/true, header->folder_id(),
                       /*item_id=*/std::string()});
    } else if (auto* row = views::AsViewClass<PinnedRowView>(child)) {
      slots.push_back({child, /*is_header=*/false, row->drag_folder_id(),
                       row->drag_item_id()});
    }
  }
  return slots;
}

bool AvoraPinnedSectionView::IsDraggedView(const views::View* view) const {
  return std::any_of(row_drag_views_.begin(), row_drag_views_.end(),
                     [view](const auto& dragged) { return dragged == view; });
}

AvoraPinnedSectionView::DropPosition
AvoraPinnedSectionView::ComputeFolderDropPosition(int local_y) const {
  // A folder always lands at the top level, between blocks: a pin on its own,
  // or another folder taken whole.  Measuring against blocks rather than rows
  // is what lets one folder step over another in a single move instead of
  // threading through its members, which are not places a folder can go.
  DropPosition position;

  const std::vector<RowSlot> slots = BuildRowSlots();
  for (size_t i = 0; i < slots.size();) {
    const size_t block_start = i;
    size_t block_end = i + 1;  // Exclusive.
    if (slots[i].is_header) {
      while (block_end < slots.size() && !slots[block_end].is_header &&
             slots[block_end].folder_id == slots[i].folder_id) {
        ++block_end;
      }
    }
    const int top = slots[block_start].view->bounds().y();
    const int bottom = slots[block_end - 1].view->bounds().bottom();
    if (local_y < (top + bottom) / 2) {
      position.before_view = slots[block_start].view.get();
      return position;
    }
    i = block_end;
  }
  return position;  // Past every block: last.
}

AvoraPinnedSectionView::DropPosition
AvoraPinnedSectionView::ComputeDropPosition(int local_y) const {
  if (!row_drag_folder_id_.empty()) {
    return ComputeFolderDropPosition(local_y);
  }

  DropPosition position;

  const std::vector<RowSlot> slots = BuildRowSlots();
  if (slots.empty()) {
    return position;  // Top-level, first position.
  }

  // Squarely over a folder header means "into this folder", the same gesture
  // the tab strip's own drag already uses to file a tab away (see
  // HighlightFolder).  Only the middle band, so the gaps at a header's top
  // and bottom edge stay available as ordinary insertion points.
  for (size_t i = 0; i < slots.size(); ++i) {
    if (!slots[i].is_header) {
      continue;
    }
    const gfx::Rect bounds = slots[i].view->bounds();
    const int band = bounds.height() / 4;
    if (local_y >= bounds.y() + band && local_y < bounds.bottom() - band) {
      position.target.folder_id = slots[i].folder_id;
      position.target.index = std::numeric_limits<int>::max();  // Appended.
      position.onto_folder_header = true;
      // Previewed after the folder's last member, which is where appending
      // puts it.  This is the only way to reach a folder's end -- the gap
      // below that last member reads as the top level below the folder, see
      // the container rule further down -- and the only way into a folder
      // that is collapsed or empty, which draws no member rows to aim
      // between.  (CommitRowDrag() opens a collapsed one on drop.)
      size_t after = i + 1;
      while (after < slots.size() && !slots[after].is_header &&
             slots[after].folder_id == slots[i].folder_id) {
        ++after;
      }
      position.before_view =
          after < slots.size() ? slots[after].view.get() : nullptr;
      return position;
    }
  }

  // Otherwise the drop lands in the gap nearest the cursor: before the first
  // row whose middle it has not yet passed.
  size_t boundary = slots.size();
  for (size_t i = 0; i < slots.size(); ++i) {
    if (local_y < slots[i].view->bounds().CenterPoint().y()) {
      boundary = i;
      break;
    }
  }

  // Which container that gap belongs to: the row *below* it decides, always.
  // A gap above a folder's member is inside that folder; a gap above a
  // folder header, or below every row, is the top level.
  //
  // So the gap under a folder's last member is the top level directly below
  // that folder -- which is a real place to be, now that folders are drawn
  // between pins rather than below all of them.  Dragging straight down out
  // of a folder walks out of it, and one row further down leaves the
  // section's last folder behind entirely; the row un-indents as you cross,
  // so the boundary shows itself.  Appending *into* a folder is the header
  // gesture above.
  if (boundary < slots.size() && !slots[boundary].is_header) {
    position.target.folder_id = slots[boundary].folder_id;
  }

  // Count the container's rows above the gap, skipping the dragged row
  // itself: MoveItemToFolderAt() takes the item out before putting it back,
  // so the index it wants is the one counted without it.
  int index = 0;
  for (size_t i = 0; i < boundary; ++i) {
    const RowSlot& slot = slots[i];
    if (slot.is_header || slot.item_id.empty() ||
        slot.folder_id != position.target.folder_id ||
        slot.item_id == row_drag_item_id_) {
      continue;
    }
    ++index;
  }
  position.target.index = index;
  position.before_view =
      boundary < slots.size() ? slots[boundary].view.get() : nullptr;
  return position;
}

void AvoraPinnedSectionView::PreviewDropAt(const DropPosition& position) {
  if (row_drag_views_.empty()) {
    return;
  }

  // The child order the drop would produce: everything else in its current
  // order, with the dragged block lifted out and set down in front of
  // |before_view| (or at the end).  Stating the whole order rather than
  // nudging indices keeps a multi-row folder block contiguous and in
  // sequence, whatever direction it is travelling.
  std::vector<views::View*> desired;
  desired.reserve(children().size());
  bool placed = false;
  for (views::View* child : children()) {
    if (IsDraggedView(child)) {
      continue;
    }
    if (child == position.before_view) {
      for (const auto& dragged : row_drag_views_) {
        desired.push_back(dragged);
      }
      placed = true;
    }
    desired.push_back(child);
  }
  if (!placed) {
    for (const auto& dragged : row_drag_views_) {
      desired.push_back(dragged);
    }
  }

  // Placing each view at its final index in order leaves the ones already
  // placed untouched, so the list ends up exactly as stated.
  for (size_t i = 0; i < desired.size(); ++i) {
    ReorderChildView(desired[i], i);
  }

  if (!row_drag_item_id_.empty()) {
    PinnedRowView* row = row_drag_views_.front();
    row->SetRowIndent(!position.target.folder_id.empty());
    // The row now genuinely lives where it is drawn, so anything reading the
    // rows -- BuildTopLevelSequence() at drop time above all -- sees the
    // arrangement the user is looking at rather than the one they started
    // from.
    row->SetDragContext(row_drag_item_id_, position.target.folder_id);
  }
}

std::vector<AvoraPinnedSectionView::TopLevelEntry>
AvoraPinnedSectionView::BuildTopLevelSequence() const {
  std::vector<TopLevelEntry> sequence;
  for (views::View* child : children()) {
    if (!child->GetVisible()) {
      continue;
    }
    if (auto* header = views::AsViewClass<FolderHeaderView>(child)) {
      sequence.push_back({/*is_folder=*/true, header->folder_id()});
    } else if (auto* row = views::AsViewClass<PinnedRowView>(child)) {
      // Members belong to their folder's own list, and a row with no item id
      // has nothing persisted to place.
      if (row->drag_folder_id().empty() && !row->drag_item_id().empty()) {
        sequence.push_back({/*is_folder=*/false, row->drag_item_id()});
      }
    }
  }
  return sequence;
}

bool AvoraPinnedSectionView::BeginRowDrag(PinnedRowView* row) {
  if (!row || row->parent() != this || !folders_manager_ ||
      !pinned_items_manager_) {
    return false;
  }

  const std::optional<size_t> index = GetIndexOf(row);
  if (!index) {
    return false;
  }

  std::string item_id;
  std::string folder_id;
  std::vector<raw_ptr<PinnedRowView>> block;
  if (auto* header = views::AsViewClass<FolderHeaderView>(row)) {
    folder_id = header->folder_id();
    // The folder travels whole: the header, then the member rows Rebuild()
    // emitted directly behind it (none, when it is collapsed or empty).
    block.push_back(row);
    for (size_t i = *index + 1; i < children().size(); ++i) {
      auto* member = views::AsViewClass<PinnedRowView>(children()[i]);
      if (!member || views::IsViewClass<FolderHeaderView>(children()[i]) ||
          member->drag_folder_id() != folder_id) {
        break;
      }
      block.push_back(member);
    }
  } else {
    // No kPinned record behind the row means no persisted position to
    // rewrite (see Rebuild() section 1a), so there is nothing to drag.
    if (row->drag_item_id().empty()) {
      return false;
    }
    item_id = row->drag_item_id();
    block.push_back(row);
  }

  // Where the block stands now, so a drag that leaves the section can put it
  // back: the view directly behind it, or none when it is already last.
  const size_t after_block = *index + block.size();
  views::View* const origin_before =
      after_block < children().size() ? children()[after_block] : nullptr;

  row_drag_active_ = true;
  row_drag_item_id_ = item_id;
  row_drag_folder_id_ = folder_id;
  row_drag_views_ = std::move(block);
  row_drag_origin_before_ = origin_before;
  row_drop_valid_ = false;
  row_drop_onto_folder_ = false;
  row->SetDragged(true);
  return true;
}

void AvoraPinnedSectionView::UpdateRowDrag(const gfx::Point& screen_point) {
  if (!row_drag_active_) {
    return;
  }

  gfx::Point local = screen_point;
  views::View::ConvertPointFromScreen(this, &local);

  // Outside the section the user is on their way somewhere else -- the
  // favorites grid above, the daily list below -- so show nothing and let
  // the release be a no-op rather than guess at a row.
  const bool inside = local.x() >= -kRowDragSlop &&
                      local.x() <= width() + kRowDragSlop &&
                      local.y() >= -kRowDragSlop &&
                      local.y() <= height() + kRowDragSlop;
  if (!inside) {
    if (row_drop_valid_) {
      // Put it back where it started, so a drag on its way to the favorites
      // grid or the daily list shows the list it is leaving unchanged rather
      // than a rearrangement that will not happen.
      row_drop_valid_ = false;
      row_drop_onto_folder_ = false;
      ClearHighlight();
      DropPosition origin;
      origin.before_view = row_drag_origin_before_;
      // The container it still belongs to as far as anything persisted is
      // concerned, which is what its depth and its context should go back to.
      if (!row_drag_item_id_.empty()) {
        origin.target.folder_id =
            folders_manager_->GetFolderIdForItem(row_drag_item_id_);
      }
      PreviewDropAt(origin);
    }
    return;
  }

  const DropPosition position = ComputeDropPosition(local.y());
  row_drop_valid_ = true;
  row_drop_target_ = position.target;
  row_drop_onto_folder_ = position.onto_folder_header;

  PreviewDropAt(position);

  if (position.onto_folder_header) {
    HighlightFolder(position.target.folder_id);
  } else {
    ClearHighlight();
  }
}

void AvoraPinnedSectionView::EndRowDrag(bool commit) {
  if (!row_drag_active_) {
    return;
  }

  const bool apply = commit && row_drop_valid_;
  const std::string item_id = apply ? row_drag_item_id_ : std::string();
  const DropTarget target = row_drop_target_;
  // Captured now, while the rows still stand where the preview put them.
  const std::vector<TopLevelEntry> sequence =
      apply ? BuildTopLevelSequence() : std::vector<TopLevelEntry>();

  for (const auto& dragged : row_drag_views_) {
    dragged->SetDragged(false);
  }
  row_drag_views_.clear();
  row_drag_origin_before_ = nullptr;
  row_drag_active_ = false;
  row_drag_item_id_.clear();
  row_drag_folder_id_.clear();
  row_drop_valid_ = false;
  row_drop_onto_folder_ = false;
  ClearHighlight();
  SchedulePaint();

  // Writing the new order reenters this view through the managers'
  // observers and rebuilds every row -- including the one still dispatching
  // the mouse release that got us here.  Defer past the event, the way
  // FolderHeaderView::CommitRename() defers its own rename for the same
  // reason.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&AvoraPinnedSectionView::FinishRowDrag,
                     weak_factory_.GetWeakPtr(), apply, item_id, target,
                     sequence));
}

void AvoraPinnedSectionView::FinishRowDrag(
    bool commit,
    const std::string& item_id,
    const DropTarget& target,
    const std::vector<TopLevelEntry>& sequence) {
  if (commit) {
    CommitRowDrag(item_id, target, sequence);
  }
  // Unconditionally, even when the commit already triggered one: a cancelled
  // drag still has to pick up whatever Rebuild() skipped while it ran.
  rebuild_pending_ = false;
  Rebuild();
}

void AvoraPinnedSectionView::CommitRowDrag(
    const std::string& item_id,
    const DropTarget& target,
    const std::vector<TopLevelEntry>& sequence) {
  if (!folders_manager_ || !pinned_items_manager_) {
    return;
  }

  // A folder moved: nothing changed hands, only the order, which
  // |sequence| already states in full.
  if (item_id.empty()) {
    ApplyTopLevelSequence(sequence);
    return;
  }

  if (!target.folder_id.empty()) {
    // Open the folder if it was closed.  A member of a collapsed folder
    // draws no row anywhere -- not here, and not in the daily strip, which
    // hides tabs this section speaks for -- so landing in one unopened
    // reads as the pin having been deleted.
    for (const auto& folder : folders_manager_->GetFolders()) {
      if (folder.id == target.folder_id && !folder.expanded) {
        folders_manager_->SetFolderExpanded(target.folder_id, true);
        break;
      }
    }
    folders_manager_->MoveItemToFolderAt(item_id, target.folder_id,
                                         target.index);
    // The item has just left the top level, so a folder anchored to it would
    // be left pointing at nothing and fall back to the end of the section.
    // |sequence| was captured with the row already previewed inside the
    // folder, so it is exactly the top-level layout that remains.
    ApplyTopLevelSequence(sequence);
    return;
  }

  // Top-level: leave whatever folder was holding it, then write back the
  // whole arrangement the drag ended on -- item order and folder placements
  // together, since dropping a pin below a folder is as much a statement
  // about the folder's place as about the pin's.  Each call no-ops when
  // nothing actually moved, so a drag that goes nowhere writes nothing.
  folders_manager_->MoveItemToFolderAt(item_id, /*folder_id=*/std::string(),
                                       /*index=*/0);
  ApplyTopLevelSequence(sequence);
}

void AvoraPinnedSectionView::ApplyTopLevelSequence(
    const std::vector<TopLevelEntry>& sequence) {
  std::vector<std::string> top_level_ids;
  std::vector<FolderPlacement> placements;
  for (const auto& entry : sequence) {
    if (entry.is_folder) {
      // An anchor of "" is a real placement -- before every item -- which is
      // exactly what a folder at the head of the sequence should get.
      placements.push_back(
          {entry.id, top_level_ids.empty()
                         ? std::optional<std::string>(std::string())
                         : std::optional<std::string>(top_level_ids.back())});
    } else {
      top_level_ids.push_back(entry.id);
    }
  }

  // Order is stored per item across the Space's whole kPinned list, so the
  // folder members interleaved in it have to be listed too or they would be
  // left holding order values that collide with the rewritten ones.  Their
  // own sequence is their folder's business (ordered_item_ids), so they just
  // follow along behind.
  std::vector<std::string> ids = top_level_ids;
  for (const auto& folder : folders_manager_->GetFolders()) {
    for (const auto& member : folder.ordered_item_ids) {
      ids.push_back(member);
    }
  }
  pinned_items_manager_->ReorderAllItems(ids);
  folders_manager_->SetFolderPlacements(placements);
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
  if (index == TabStripModel::kNoTab) {
    return;
  }
  // Clicking a parked pin is asking for its page back.  Unparking rebuilds
  // this section (and deletes the clicked row), but |contents| is the tab's,
  // not the row's, so it is still good for the activation below.
  UnparkPinnedTab(tab_strip_model_, contents);
  tab_strip_model_->ActivateTabAt(
      tab_strip_model_->GetIndexOfWebContents(contents));
}

void AvoraPinnedSectionView::OnPinnedTabCloseRequested(
    content::WebContents* contents) {
  if (!tab_strip_model_ || !contents) {
    return;
  }
  const int index = tab_strip_model_->GetIndexOfWebContents(contents);
  if (index == TabStripModel::kNoTab) {
    return;
  }

  // Nothing persisted would bring this pin back once its tab is gone, so the
  // tab stays and only its page is set aside.  Parking notifies the tab
  // strip, whose kAll change rebuilds this section and deletes the row whose
  // minus was clicked, so nothing may follow this.
  if (GetPinnedItemIdForTab(contents).empty()) {
    ParkPinnedTab(tab_strip_model_, contents);
    return;
  }

  // Hand focus back first, so the tab being closed is already in the
  // background when it goes: TabStripModel then has no successor of its own to
  // pick, and nothing in between ever flashes on screen.
  if (index == tab_strip_model_->active_index()) {
    if (tabs::TabInterface* previous =
            FindPreviouslyActiveTab(tab_strip_model_->GetTabAtIndex(index))) {
      tab_strip_model_->ActivateTab(previous);
    }
  }

  // Only the live tab goes.  The kPinned item is persisted separately, so the
  // removal's Rebuild() draws it again as a tab-less row that rematerializes
  // on the next click.  That Rebuild() also deletes the row whose minus was
  // clicked, so nothing may follow this.
  const int close_index = tab_strip_model_->GetIndexOfWebContents(contents);
  if (close_index != TabStripModel::kNoTab) {
    tab_strip_model_->CloseWebContentsAt(close_index,
                                         TabCloseTypes::CLOSE_USER_GESTURE);
  }
}

void AvoraPinnedSectionView::RememberDeactivatedTab(tabs::TabInterface* tab) {
  const tabs::TabHandle handle = tab->GetHandle();
  std::erase_if(activation_history_, [&](const tabs::TabHandle& entry) {
    return entry == handle || entry.Get() == nullptr;
  });
  activation_history_.insert(activation_history_.begin(), handle);
  if (activation_history_.size() > kActivationHistoryLimit) {
    activation_history_.resize(kActivationHistoryLimit);
  }
}

tabs::TabInterface* AvoraPinnedSectionView::FindPreviouslyActiveTab(
    tabs::TabInterface* closing) {
  const std::string active_space =
      pinned_items_manager_ ? pinned_items_manager_->GetActiveSpaceId()
                            : std::string();
  for (const tabs::TabHandle& handle : activation_history_) {
    tabs::TabInterface* tab = handle.Get();
    // Gone, the one being closed, or dragged off to another window since.
    if (!tab || tab == closing || tab_strip_model_->GetIndexOfTab(tab) < 0) {
      continue;
    }
    content::WebContents* contents = tab->GetContents();
    // A tab another Space owns is hidden from this window's strip, and a link
    // preview is a glance that has since closed or been promoted -- neither
    // is somewhere the user was working.
    if (!contents || !TabBelongsToSpace(contents, active_space) ||
        IsLinkPreviewTab(contents)) {
      continue;
    }
    return tab;
  }
  return nullptr;
}

void AvoraPinnedSectionView::OnPinnedTabReturnToTarget(
    content::WebContents* contents) {
  if (!tab_strip_model_ || !contents) {
    return;
  }

  // Navigate before activating.  ActivateTabAt() notifies this view
  // synchronously, and Rebuild() then deletes the row whose favicon is
  // mid-click, so anything this call still needs has to happen first.
  ReturnTabToPinnedTarget(pinned_items_manager_.get(), contents);

  const int index = tab_strip_model_->GetIndexOfWebContents(contents);
  if (index != TabStripModel::kNoTab) {
    tab_strip_model_->ActivateTabAt(index);
  }
}

void AvoraPinnedSectionView::ApplyPinnedTargetState(PinnedTabRow* row) {
  content::WebContents* contents = row ? row->web_contents() : nullptr;
  if (!contents) {
    return;
  }
  const GURL target =
      GetPinnedTargetForTab(pinned_items_manager_.get(), contents);
  if (!target.is_valid()) {
    // A natively pinned tab with no kPinned record behind it has no target
    // to have left, so it keeps the plain favicon it has always had.
    return;
  }
  row->SetPinnedTarget(
      target, !IsAtPinnedTarget(target, contents->GetLastCommittedURL()));
}

void AvoraPinnedSectionView::OnPinnedTabRenamed(
    content::WebContents* contents,
    const std::u16string& new_title) {
  if (!contents || !folders_manager_) {
    return;
  }

  // The name belongs to the pin, not to the page the tab is showing when the
  // rename happens.  Keying it by the live URL meant two pins parked on one
  // page -- both AWS accounts sitting on the same SSO sign-in URL, say --
  // shared a key, so renaming either renamed both.  The target is the
  // fallback for a row with no pinned item at all; the live URL is never a
  // key again.
  const std::string item_id = GetPinnedItemIdForTab(contents);
  const GURL target =
      GetPinnedTargetForTab(pinned_items_manager_.get(), contents);
  folders_manager_->SetCustomTitleForItem(
      item_id,
      target.is_valid() ? target.spec()
                        : contents->GetLastCommittedURL().spec(),
      base::UTF16ToUTF8(new_title));
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

  // The pin's own target, not wherever its tab has wandered to: a pinned row
  // keeps the identity it was pinned with, and the away marker -- not a
  // different favicon -- is what says the page underneath has changed.
  const GURL icon_url = row->target_url().is_valid()
                            ? row->target_url()
                            : row->web_contents()->GetLastCommittedURL();
  favicon_service->GetRawFaviconForPageURL(
      icon_url,
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
