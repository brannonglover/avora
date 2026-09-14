// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/avora/avora_sidebar_view.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/avora/avora_defaults.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "components/vector_icons/vector_icons.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/image_loader.h"
#include "extensions/browser/ui_util.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_handlers/icons_handler.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/focus/focus_manager.h"

namespace {

constexpr SkColor kAvoraSidebarBg = SkColorSetRGB(0x18, 0x1C, 0x20);
constexpr SkColor kAvoraIconColor = SkColorSetRGB(0xED, 0xF2, 0xF5);
constexpr SkColor kAvoraAddressChipBg = SkColorSetRGB(0x32, 0x38, 0x40);
constexpr int kNavButtonSize = 32;
constexpr int kNavIconSize = 20;
constexpr int kNavButtonGap = 4;
constexpr int kSidebarHPadding = 10;
constexpr int kAddressChipMinHeight = 36;

// ── Manage bubble colours ───────────────────────────────────────────────────
constexpr SkColor kBubbleBg = SkColorSetRGB(0x22, 0x28, 0x2E);
constexpr SkColor kBubbleSectionText = SkColorSetARGB(0x88, 0xED, 0xF2, 0xF5);
constexpr SkColor kBubbleRowText = SkColorSetRGB(0xED, 0xF2, 0xF5);
constexpr SkColor kBubbleRowHoverBg = SkColorSetARGB(0x18, 0xFF, 0xFF, 0xFF);
constexpr SkColor kBubbleRowIconColor = SkColorSetARGB(0xBB, 0xED, 0xF2, 0xF5);
constexpr int kBubbleWidth = 220;
constexpr int kBubbleRowHeight = 36;
constexpr int kBubbleIconSize = 16;
constexpr int kExtIconButtonSize = 32;
constexpr int kExtIconImageSize = 20;
constexpr int kExtIconGap = 6;

std::unique_ptr<views::View> MakeSectionPlaceholder(int height) {
  auto section = std::make_unique<views::View>();
  section->SetPreferredSize(gfx::Size(0, height));
  return section;
}

std::unique_ptr<views::ImageButton> MakeNavButton(
    const gfx::VectorIcon& icon,
    base::RepeatingClosure callback,
    const std::u16string& tooltip) {
  auto button = std::make_unique<views::ImageButton>(std::move(callback));
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(icon, kAvoraIconColor, kNavIconSize));
  button->SetPreferredSize(gfx::Size(kNavButtonSize, kNavButtonSize));
  button->SetTooltipText(tooltip);
  return button;
}

// Button that hides itself when the mouse leaves both it AND its parent chip.
class HoverRevealButton : public views::ImageButton {
  METADATA_HEADER(HoverRevealButton, views::ImageButton)
 public:
  HoverRevealButton(PressedCallback cb, views::View* chip_parent)
      : views::ImageButton(std::move(cb)), chip_parent_(chip_parent) {}

  void OnMouseExited(const ui::MouseEvent& event) override {
    views::ImageButton::OnMouseExited(event);
    if (chip_parent_ && !chip_parent_->IsMouseHovered()) {
      SetVisible(false);
    }
  }

 private:
  raw_ptr<views::View> chip_parent_;
};

BEGIN_METADATA(HoverRevealButton)
END_METADATA

class AddressChipView : public views::View {
  METADATA_HEADER(AddressChipView, views::View)
 public:
  using ClickCallback = base::RepeatingClosure;
  explicit AddressChipView(ClickCallback callback)
      : callback_(std::move(callback)) {}

  void SetHoverButton(views::View* button) { hover_button_ = button; }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (HitTestPoint(event.location()) && callback_) {
      callback_.Run();
    }
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    if (hover_button_) {
      hover_button_->SetVisible(true);
    }
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    if (hover_button_ && !hover_button_->IsMouseHovered()) {
      hover_button_->SetVisible(false);
    }
  }

 private:
  ClickCallback callback_;
  raw_ptr<views::View> hover_button_ = nullptr;
};

BEGIN_METADATA(AddressChipView)
END_METADATA

// ── Manage bubble row (icon + label, hover highlight) ───────────────────────

class ManageBubbleRow : public views::LabelButton {
  METADATA_HEADER(ManageBubbleRow, views::LabelButton)

 public:
  ManageBubbleRow(const gfx::VectorIcon& icon,
                  const std::u16string& text,
                  views::Button::PressedCallback callback)
      : views::LabelButton(std::move(callback), text) {
    SetImageModel(views::Button::STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(icon, kBubbleRowIconColor,
                                                 kBubbleIconSize));
    SetTextColor(views::Button::STATE_NORMAL, kBubbleRowText);
    SetTextColor(views::Button::STATE_HOVERED, kBubbleRowText);
    SetTextColor(views::Button::STATE_PRESSED, kBubbleRowText);
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 12)));
    SetPreferredSize(gfx::Size(kBubbleWidth, kBubbleRowHeight));
    SetFocusBehavior(FocusBehavior::ALWAYS);
  }

  ~ManageBubbleRow() override = default;

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (GetState() == views::Button::STATE_HOVERED ||
        GetState() == views::Button::STATE_PRESSED) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(kBubbleRowHoverBg);
      flags.setStyle(cc::PaintFlags::kFill_Style);
      canvas->DrawRoundRect(GetLocalBounds(), 6, flags);
    }
  }
};

BEGIN_METADATA(ManageBubbleRow)
END_METADATA


}  // namespace

// --- AvoraSidebarView -------------------------------------------------------

AvoraSidebarView::AvoraSidebarView(Profile* profile,
                                   AvoraNavCallback back_cb,
                                   AvoraNavCallback forward_cb,
                                   AvoraNavCallback reload_cb,
                                   AvoraExtensionClickCallback extension_click_cb,
                                   AvoraNavCallback extensions_cb,
                                   AvoraNavCallback webstore_cb,
                                   AvoraNavCallback settings_cb,
                                   AvoraAddressCommitCallback address_commit_cb)
    : profile_(profile),
      extension_click_cb_(std::move(extension_click_cb)),
      extensions_cb_(std::move(extensions_cb)),
      webstore_cb_(std::move(webstore_cb)),
      settings_cb_(std::move(settings_cb)),
      address_commit_cb_(std::move(address_commit_cb)) {
  avora::ApplyBrowserDefaults(profile_->GetPrefs());

  SetBackground(views::CreateSolidBackground(kAvoraSidebarBg));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // --- Nav row: back/forward/reload right-aligned (inline with stoplight) ---
  nav_row_ = AddChildView(MakeSectionPlaceholder(kNavRowHeight));
  auto* nav_layout =
      nav_row_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::TLBR(10, kSidebarHPadding, 0, 2),
          kNavButtonGap));
  nav_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kEnd);
  nav_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  back_button_ = nav_row_->AddChildView(
      MakeNavButton(vector_icons::kArrowBackIcon, std::move(back_cb),
                    u"Back"));
  forward_button_ = nav_row_->AddChildView(
      MakeNavButton(vector_icons::kArrowForwardIcon, std::move(forward_cb),
                    u"Forward"));
  reload_button_ = nav_row_->AddChildView(
      MakeNavButton(vector_icons::kRefreshIcon, std::move(reload_cb),
                    u"Reload"));

  // --- Address chip: full-width clickable pill showing current URL ---
  address_chip_row_ =
      AddChildView(MakeSectionPlaceholder(kAddressChipRowHeight));
  {
    auto* chip_layout =
        address_chip_row_->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal,
            gfx::Insets::TLBR(6, kSidebarHPadding, 6, kSidebarHPadding)));
    chip_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    auto chip_bg = std::make_unique<AddressChipView>(
        base::BindRepeating(&AvoraSidebarView::BeginEditing,
                            base::Unretained(this)));
    chip_bg->SetBackground(views::CreateRoundedRectBackground(
        kAvoraAddressChipBg, 10));
    chip_bg->SetPreferredSize(gfx::Size(0, kAddressChipMinHeight));
    chip_bg->SetFocusBehavior(FocusBehavior::NEVER);
    auto* chip_inner =
        chip_bg->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal,
            gfx::Insets::VH(8, 12)));
    chip_inner->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    // URL label (visible in display mode).
    auto label = std::make_unique<views::Label>(
        u"New Tab",
        views::Label::CustomFont{gfx::FontList({std::string("system-ui")},
                                                gfx::Font::NORMAL, 15,
                                                gfx::Font::Weight::NORMAL)});
    label->SetEnabledColor(kAvoraIconColor);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetElideBehavior(gfx::ELIDE_TAIL);
    address_label_ = chip_bg->AddChildView(std::move(label));
    chip_inner->SetFlexForView(address_label_, 1);

    // Inline textfield (hidden until clicked).
    auto field = std::make_unique<views::Textfield>();
    field->SetFontList(gfx::FontList({std::string("system-ui")},
                                     gfx::Font::NORMAL, 15,
                                     gfx::Font::Weight::NORMAL));
    field->SetColor(kAvoraIconColor);
    field->SetBackgroundColor(kAvoraAddressChipBg);
    field->SetBorder(nullptr);
    field->SetPlaceholderText(u"Search or enter URL");
    field->SetVisible(false);
    address_field_ = chip_bg->AddChildView(std::move(field));
    address_field_->set_controller(this);
    chip_inner->SetFlexForView(address_field_, 1);

    auto hover_btn = std::make_unique<HoverRevealButton>(
        base::BindRepeating(&AvoraSidebarView::ShowManageBubble,
                            base::Unretained(this)),
        chip_bg.get());
    hover_btn->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(vector_icons::kTuneIcon,
                                       kAvoraIconColor, kNavIconSize));
    hover_btn->SetPreferredSize(gfx::Size(kNavButtonSize, kNavButtonSize));
    hover_btn->SetTooltipText(u"Manage");
    hover_btn->SetVisible(false);
    manage_button_ = chip_bg->AddChildView(std::move(hover_btn));

    chip_bg->SetHoverButton(manage_button_);

    address_chip_bg_ = address_chip_row_->AddChildView(std::move(chip_bg));
    chip_layout->SetFlexForView(address_chip_bg_, 1);
  }

  // --- Create SpaceManager (owned here; shared with the spaces bar) ---
  space_manager_ =
      std::make_unique<avora::SpaceManager>(profile_->GetPrefs());
}

AvoraSidebarView::~AvoraSidebarView() = default;

void AvoraSidebarView::SetURL(const std::u16string& url_text) {
  current_url_text_ = url_text;
  if (address_label_) {
    address_label_->SetText(url_text.empty() ? u"New Tab" : url_text);
  }
}

void AvoraSidebarView::ShowManageBubble() {
  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      manage_button_, views::BubbleBorder::TOP_RIGHT);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->set_margins(gfx::Insets::VH(8, 0));
  delegate->SetTitle(u"");
  delegate->SetShowTitle(false);
  delegate->SetShowCloseButton(false);
  delegate->SetBackgroundColor(kBubbleBg);

  auto wrapper = std::make_unique<views::View>();
  auto* layout = wrapper->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto* delegate_raw = delegate.get();

  // ── Extensions section header ──
  auto section_label = std::make_unique<views::Label>(
      u"Extensions",
      views::Label::CustomFont{gfx::FontList({std::string("system-ui")},
                                              gfx::Font::NORMAL, 11,
                                              gfx::Font::Weight::MEDIUM)});
  section_label->SetEnabledColor(kBubbleSectionText);
  section_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  section_label->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(4, 12, 4, 12)));
  wrapper->AddChildView(std::move(section_label));

  // ── Extension icons row (horizontal: [icon] [icon] ... [+]) ──
  auto icons_row = std::make_unique<views::View>();
  auto* icons_layout =
      icons_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(4, 12), kExtIconGap));
  icons_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  if (profile_) {
    auto* registry = extensions::ExtensionRegistry::Get(profile_);
    auto* action_manager =
        extensions::ExtensionActionManager::Get(profile_);
    if (registry) {
      for (const auto& ext : registry->enabled_extensions()) {
        if (!ext->is_extension()) {
          continue;
        }
        if (!extensions::ui_util::ShouldDisplayInExtensionSettings(*ext)) {
          continue;
        }
        std::u16string name = base::UTF8ToUTF16(ext->name());

        auto icon_btn = std::make_unique<views::ImageButton>(
            base::BindRepeating(
                [](views::BubbleDialogDelegate* d,
                   AvoraExtensionClickCallback cb,
                   const std::string& id) {
                  if (d->GetWidget()) d->GetWidget()->Close();
                  // Post asynchronously so the bubble fully tears down before
                  // the extension popup opens (avoids widget activation
                  // conflicts that prevent the popup WebContents from loading).
                  if (cb) {
                    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                        FROM_HERE, base::BindOnce(cb, id));
                  }
                },
                base::Unretained(delegate_raw),
                extension_click_cb_,
                ext->id()));
        icon_btn->SetPreferredSize(
            gfx::Size(kExtIconButtonSize, kExtIconButtonSize));
        icon_btn->SetTooltipText(name);

        // Use the pre-loaded icon from ExtensionAction (same source as the
        // toolbar).  This is synchronous and avoids the race between async
        // image loading and the bubble's transient lifetime.
        bool icon_set = false;
        if (action_manager) {
          auto* action = action_manager->GetExtensionAction(*ext);
          if (action) {
            gfx::Image icon = action->GetDefaultIconImage();
            if (!icon.IsEmpty()) {
              icon_btn->SetImageModel(
                  views::Button::STATE_NORMAL,
                  ui::ImageModel::FromImage(icon));
              icon_set = true;
            }
          }
        }

        // Fallback: load from the manifest "icons" field.
        if (!icon_set) {
          icon_btn->SetImageModel(
              views::Button::STATE_NORMAL,
              ui::ImageModel::FromVectorIcon(
                  vector_icons::kChromeExtensionIcon, kBubbleRowIconColor,
                  kExtIconImageSize));

          auto* image_loader = extensions::ImageLoader::Get(profile_);
          if (image_loader) {
            extensions::ExtensionResource icon_resource =
                extensions::IconsInfo::GetIconResource(
                    ext.get(), kExtIconImageSize,
                    ExtensionIconSet::Match::kBigger);
            if (icon_resource.empty()) {
              icon_resource = extensions::IconsInfo::GetIconResource(
                  ext.get(), kExtIconImageSize,
                  ExtensionIconSet::Match::kSmaller);
            }
            if (!icon_resource.empty()) {
              auto* icon_btn_raw =
                  icons_row->AddChildView(std::move(icon_btn));
              image_loader->LoadImageAsync(
                  ext.get(), icon_resource,
                  gfx::Size(kExtIconImageSize, kExtIconImageSize),
                  base::BindOnce(
                      [](base::WeakPtr<AvoraSidebarView> self,
                         views::ImageButton* btn,
                         const gfx::Image& image) {
                        if (!self || image.IsEmpty()) {
                          return;
                        }
                        btn->SetImageModel(
                            views::Button::STATE_NORMAL,
                            ui::ImageModel::FromImage(image));
                      },
                      weak_ptr_factory_.GetWeakPtr(),
                      base::Unretained(icon_btn_raw)));
              continue;
            }
          }
        }

        icons_row->AddChildView(std::move(icon_btn));
      }
    }
  }

  // "+" button at the end of the icon row.
  AvoraNavCallback ws_cb = webstore_cb_;
  auto plus_btn = std::make_unique<views::ImageButton>(
      base::BindRepeating(
          [](views::BubbleDialogDelegate* d, AvoraNavCallback cb) {
            if (d->GetWidget()) d->GetWidget()->Close();
            if (cb) cb.Run();
          },
          base::Unretained(delegate_raw), ws_cb));
  plus_btn->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(vector_icons::kAddOldIcon,
                                     kBubbleSectionText, kExtIconImageSize));
  plus_btn->SetPreferredSize(
      gfx::Size(kExtIconButtonSize, kExtIconButtonSize));
  plus_btn->SetTooltipText(u"Add Extensions");
  icons_row->AddChildView(std::move(plus_btn));

  wrapper->AddChildView(std::move(icons_row));

  // ── Manage Extensions row ──
  wrapper->AddChildView(std::make_unique<ManageBubbleRow>(
      vector_icons::kChromeExtensionIcon, u"Manage Extensions\u2026",
      base::BindRepeating(
          [](views::BubbleDialogDelegate* d, AvoraNavCallback cb) {
            if (d->GetWidget()) d->GetWidget()->Close();
            if (cb) cb.Run();
          },
          base::Unretained(delegate_raw), extensions_cb_)));

  // ── Separator ──
  auto sep = std::make_unique<views::Separator>();
  sep->SetColorId(ui::kColorSeparator);
  sep->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 12)));
  wrapper->AddChildView(std::move(sep));

  // ── Settings section header ──
  auto settings_label = std::make_unique<views::Label>(
      u"Settings",
      views::Label::CustomFont{gfx::FontList({std::string("system-ui")},
                                              gfx::Font::NORMAL, 11,
                                              gfx::Font::Weight::MEDIUM)});
  settings_label->SetEnabledColor(kBubbleSectionText);
  settings_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  settings_label->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(4, 12, 4, 12)));
  wrapper->AddChildView(std::move(settings_label));

  // ── Settings row ──
  AvoraNavCallback set_cb = settings_cb_;
  wrapper->AddChildView(std::make_unique<ManageBubbleRow>(
      vector_icons::kSettingsIcon, u"Avora Settings",
      base::BindRepeating(
          [](views::BubbleDialogDelegate* d, AvoraNavCallback cb) {
            if (d->GetWidget()) d->GetWidget()->Close();
            if (cb) cb.Run();
          },
          base::Unretained(delegate_raw), set_cb)));

  delegate->SetContentsView(std::move(wrapper));

  auto* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(delegate),
      views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  widget->Show();
}

void AvoraSidebarView::BeginEditing() {
  if (!address_label_ || !address_field_) {
    return;
  }
  address_label_->SetVisible(false);
  address_field_->SetVisible(true);
  address_field_->SetText(current_url_text_);
  address_field_->SelectAll(true);
  address_field_->RequestFocus();
}

void AvoraSidebarView::EndEditing() {
  if (!address_label_ || !address_field_) {
    return;
  }
  address_field_->SetVisible(false);
  address_label_->SetVisible(true);
  address_chip_bg_->InvalidateLayout();
}

bool AvoraSidebarView::HandleKeyEvent(views::Textfield* sender,
                                      const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }

  if (key_event.key_code() == ui::VKEY_RETURN) {
    std::u16string text(sender->GetText());
    EndEditing();
    if (!text.empty() && address_commit_cb_) {
      address_commit_cb_.Run(text);
    }
    return true;
  }

  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    EndEditing();
    return true;
  }

  return false;
}

bool AvoraSidebarView::IsPositionInWindowCaption(
    const gfx::Point& point) const {
  for (views::View* child : children()) {
    if (!child->GetVisible()) {
      continue;
    }
    gfx::Point point_in_child = point;
    views::View::ConvertPointToTarget(this, child, &point_in_child);
    if (!child->HitTestPoint(point_in_child)) {
      continue;
    }
    for (views::View* grandchild : child->children()) {
      if (!grandchild->GetVisible()) {
        continue;
      }
      gfx::Point pt = point_in_child;
      views::View::ConvertPointToTarget(child, grandchild, &pt);
      if (grandchild->HitTestPoint(pt)) {
        return false;
      }
    }
    return true;
  }
  return true;
}

BEGIN_METADATA(AvoraSidebarView)
END_METADATA

