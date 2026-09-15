// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_space_icon_picker.h"

#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/avora/avora_space_icons.h"
#include "chrome/browser/ui/views/avora/avora_lucide_icon.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace avora {

namespace {

constexpr SkColor kBubbleBg = SkColorSetRGB(0x1B, 0x1F, 0x26);
constexpr SkColor kFieldBg = SkColorSetRGB(0x26, 0x2C, 0x34);
constexpr SkColor kPrimaryText = SkColorSetRGB(0xED, 0xF2, 0xF5);
constexpr SkColor kSecondaryText = SkColorSetARGB(0x99, 0xED, 0xF2, 0xF5);
constexpr SkColor kHoverBg = SkColorSetARGB(0x1A, 0xFF, 0xFF, 0xFF);
constexpr SkColor kSelectionRing = SkColorSetARGB(0xFF, 0xFF, 0xFF, 0xFF);

constexpr int kContentWidth = 300;
constexpr int kGridColumns = 8;
constexpr int kIconButtonSize = 34;
constexpr int kIconGlyphSize = 20;
constexpr int kIconButtonRadius = 8;
constexpr int kGridGap = 4;
constexpr int kSwatchSize = 22;
constexpr int kSwatchDotSize = 14;
constexpr int kPreviewSize = 44;
constexpr int kPreviewGlyphSize = 26;
constexpr int kRowGap = 8;
constexpr int kGridMinHeight = 160;
constexpr int kGridMaxHeight = 264;

gfx::FontList UiFont(int size, gfx::Font::Weight weight) {
  return gfx::FontList({std::string("system-ui")}, gfx::Font::NORMAL, size,
                       weight);
}

void FillRoundRect(gfx::Canvas* canvas,
                   const gfx::Rect& bounds,
                   SkColor color,
                   int radius) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(color);
  canvas->DrawRoundRect(bounds, radius, flags);
}

// Shows the icon exactly as the Spaces bar will draw it, so the accent colour
// and icon choice can be judged together.
class SpaceIconPreview : public views::View {
  METADATA_HEADER(SpaceIconPreview, views::View)

 public:
  SpaceIconPreview(std::string icon_id, SkColor accent)
      : icon_id_(std::move(icon_id)), accent_(accent) {
    SetPreferredSize(gfx::Size(kPreviewSize, kPreviewSize));
  }

  void SetIcon(std::string icon_id) {
    icon_id_ = std::move(icon_id);
    SchedulePaint();
  }

  void SetAccent(SkColor accent) {
    accent_ = accent;
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);
    FillRoundRect(canvas, GetLocalBounds(), SkColorSetA(accent_, 0x2E), 12);
    gfx::Rect glyph(GetLocalBounds());
    glyph.ClampToCenteredSize(gfx::Size(kPreviewGlyphSize, kPreviewGlyphSize));
    PaintLucideIcon(canvas, glyph, icon_id_, accent_);
  }

 private:
  std::string icon_id_;
  SkColor accent_;
};

BEGIN_METADATA(SpaceIconPreview)
END_METADATA

class IconGridButton : public views::Button {
  METADATA_HEADER(IconGridButton, views::Button)

 public:
  IconGridButton(PressedCallback callback,
                 std::string icon_id,
                 const std::u16string& label,
                 SkColor accent,
                 bool selected)
      : views::Button(std::move(callback)),
        icon_id_(std::move(icon_id)),
        accent_(accent),
        selected_(selected) {
    SetPreferredSize(gfx::Size(kIconButtonSize, kIconButtonSize));
    SetTooltipText(label);
    GetViewAccessibility().SetName(label);
    SetFocusBehavior(FocusBehavior::ALWAYS);
  }

  const std::string& icon_id() const { return icon_id_; }

  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    SchedulePaint();
  }

  void SetAccent(SkColor accent) {
    accent_ = accent;
    SchedulePaint();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const bool hovered = GetState() == views::Button::STATE_HOVERED ||
                         GetState() == views::Button::STATE_PRESSED;
    if (selected_) {
      FillRoundRect(canvas, GetLocalBounds(), SkColorSetA(accent_, 0x38),
                    kIconButtonRadius);
      cc::PaintFlags ring;
      ring.setAntiAlias(true);
      ring.setStyle(cc::PaintFlags::kStroke_Style);
      ring.setStrokeWidth(1.5f);
      ring.setColor(SkColorSetA(kSelectionRing, 0x66));
      gfx::RectF inset(GetLocalBounds());
      inset.Inset(0.75f);
      canvas->DrawRoundRect(inset, kIconButtonRadius, ring);
    } else if (hovered) {
      FillRoundRect(canvas, GetLocalBounds(), kHoverBg, kIconButtonRadius);
    }

    gfx::Rect glyph(GetLocalBounds());
    glyph.ClampToCenteredSize(gfx::Size(kIconGlyphSize, kIconGlyphSize));
    PaintLucideIcon(canvas, glyph, icon_id_,
                    selected_ ? accent_ : SkColorSetA(kPrimaryText, 0xCC));
  }

 private:
  std::string icon_id_;
  SkColor accent_;
  bool selected_ = false;
};

BEGIN_METADATA(IconGridButton)
END_METADATA

class AccentSwatchButton : public views::Button {
  METADATA_HEADER(AccentSwatchButton, views::Button)

 public:
  AccentSwatchButton(PressedCallback callback,
                     std::string hex,
                     SkColor color,
                     bool selected)
      : views::Button(std::move(callback)),
        hex_(std::move(hex)),
        color_(color),
        selected_(selected) {
    SetPreferredSize(gfx::Size(kSwatchSize, kSwatchSize));
    SetTooltipText(base::UTF8ToUTF16(hex_));
    GetViewAccessibility().SetName(base::UTF8ToUTF16(hex_));
    SetFocusBehavior(FocusBehavior::ALWAYS);
  }

  const std::string& hex() const { return hex_; }

  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    SchedulePaint();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::PointF center(GetLocalBounds().CenterPoint());

    cc::PaintFlags dot;
    dot.setAntiAlias(true);
    dot.setStyle(cc::PaintFlags::kFill_Style);
    dot.setColor(color_);
    canvas->DrawCircle(center, kSwatchDotSize / 2.0f, dot);

    if (selected_) {
      cc::PaintFlags ring;
      ring.setAntiAlias(true);
      ring.setStyle(cc::PaintFlags::kStroke_Style);
      ring.setStrokeWidth(1.5f);
      ring.setColor(color_);
      canvas->DrawCircle(center, kSwatchSize / 2.0f - 1.0f, ring);
    }
  }

 private:
  std::string hex_;
  SkColor color_;
  bool selected_ = false;
};

BEGIN_METADATA(AccentSwatchButton)
END_METADATA

// Name field, accent palette, and the curated Lucide grid.
class SpaceEditorView : public views::View, public views::TextfieldController {
  METADATA_HEADER(SpaceEditorView, views::View)

 public:
  explicit SpaceEditorView(const SpaceEditorFields& initial)
      : icon_(NormalizeSpaceIconId(initial.icon)),
        accent_color_(NormalizeSpaceAccentColor(initial.accent_color)) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), kRowGap));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    AddChildView(BuildIdentityRow(initial.name));
    AddChildView(BuildAccentRow());
    AddChildView(BuildSearchField());
    AddChildView(BuildIconGrid());
  }

  SpaceEditorFields fields() const {
    SpaceEditorFields result;
    result.name = base::UTF16ToUTF8(base::TrimWhitespace(
        name_field_->GetText(), base::TRIM_ALL));
    result.icon = icon_;
    result.accent_color = accent_color_;
    return result;
  }

  void FocusNameField() {
    name_field_->RequestFocus();
    name_field_->SelectAll(true);
  }

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override {
    if (sender == search_field_) {
      RebuildIconGrid();
    }
  }

  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override {
    if (key_event.type() != ui::EventType::kKeyPressed ||
        key_event.key_code() != ui::VKEY_RETURN) {
      return false;
    }
    views::Widget* widget = GetWidget();
    views::DialogDelegate* dialog =
        widget ? widget->widget_delegate()->AsDialogDelegate() : nullptr;
    if (!dialog) {
      return false;
    }
    dialog->AcceptDialog();
    return true;
  }

 private:
  std::unique_ptr<views::View> BuildIdentityRow(const std::string& name) {
    auto row = std::make_unique<views::View>();
    auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kRowGap));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    preview_ = row->AddChildView(
        std::make_unique<SpaceIconPreview>(icon_, AccentColor()));

    auto field = std::make_unique<views::Textfield>();
    field->SetController(this);
    field->SetText(base::UTF8ToUTF16(name));
    field->SetPlaceholderText(u"Space name");
    field->SetFontList(UiFont(13, gfx::Font::Weight::MEDIUM));
    field->SetColor(kPrimaryText);
    field->SetBackgroundColor(kFieldBg);
    field->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(7, 10)));
    field->GetViewAccessibility().SetName(u"Space name");
    name_field_ = row->AddChildView(std::move(field));
    layout->SetFlexForView(name_field_, 1);

    return row;
  }

  std::unique_ptr<views::View> BuildAccentRow() {
    auto row = std::make_unique<views::View>();
    auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kGridGap));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    for (std::string_view hex : GetSpaceAccentColors()) {
      const std::string color(hex);
      swatch_buttons_.push_back(row->AddChildView(
          std::make_unique<AccentSwatchButton>(
              base::BindRepeating(&SpaceEditorView::OnAccentSelected,
                                  base::Unretained(this), color),
              color, ParseSpaceAccentColor(color, kPrimaryText),
              color == accent_color_)));
    }
    return row;
  }

  std::unique_ptr<views::View> BuildSearchField() {
    auto field = std::make_unique<views::Textfield>();
    field->SetController(this);
    field->SetPlaceholderText(u"Search icons");
    field->SetFontList(UiFont(12, gfx::Font::Weight::NORMAL));
    field->SetColor(kPrimaryText);
    field->SetBackgroundColor(kFieldBg);
    field->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(6, 10)));
    field->GetViewAccessibility().SetName(u"Search icons");
    search_field_ = field.get();
    return field;
  }

  std::unique_ptr<views::View> BuildIconGrid() {
    auto scroll_view = std::make_unique<views::ScrollView>();
    scroll_view->ClipHeightTo(kGridMinHeight, kGridMaxHeight);
    scroll_view->SetBackgroundColor(std::nullopt);
    scroll_view->SetHorizontalScrollBarMode(
        views::ScrollView::ScrollBarMode::kDisabled);
    scroll_view->SetDrawOverflowIndicator(false);

    auto contents = std::make_unique<views::View>();
    auto* layout =
        contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(),
            kGridGap));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStart);
    grid_container_ = scroll_view->SetContents(std::move(contents));

    RebuildIconGrid();
    return scroll_view;
  }

  // Rebuilds the sections from scratch: cheap at this catalog size, and it
  // keeps filtering, grouping, and row wrapping in one place.
  void RebuildIconGrid() {
    icon_buttons_.clear();
    grid_container_->RemoveAllChildViews();

    const std::u16string_view search_text =
        search_field_ ? std::u16string_view(search_field_->GetText())
                      : std::u16string_view();
    const std::string query = base::ToLowerASCII(base::UTF16ToUTF8(
        base::TrimWhitespace(search_text, base::TRIM_ALL)));

    std::string_view section;
    views::View* row = nullptr;
    int column = 0;

    for (const SpaceIcon& icon : GetSpaceIconCatalog()) {
      if (!Matches(icon, query)) {
        continue;
      }
      if (icon.category != section) {
        section = icon.category;
        grid_container_->AddChildView(MakeSectionLabel(section));
        row = nullptr;
      }
      if (!row || column == kGridColumns) {
        row = grid_container_->AddChildView(MakeGridRow());
        column = 0;
      }
      icon_buttons_.push_back(row->AddChildView(
          std::make_unique<IconGridButton>(
              base::BindRepeating(&SpaceEditorView::OnIconSelected,
                                  base::Unretained(this),
                                  std::string(icon.id)),
              std::string(icon.id), base::UTF8ToUTF16(icon.label),
              AccentColor(), icon.id == icon_)));
      ++column;
    }

    if (icon_buttons_.empty()) {
      auto empty = std::make_unique<views::Label>(
          u"No icons match that search",
          views::Label::CustomFont{UiFont(12, gfx::Font::Weight::NORMAL)});
      empty->SetEnabledColor(kSecondaryText);
      empty->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      grid_container_->AddChildView(std::move(empty));
    }

    grid_container_->InvalidateLayout();
  }

  static bool Matches(const SpaceIcon& icon, const std::string& query) {
    if (query.empty()) {
      return true;
    }
    return base::ToLowerASCII(icon.id).find(query) != std::string::npos ||
           base::ToLowerASCII(icon.label).find(query) != std::string::npos ||
           base::ToLowerASCII(icon.category).find(query) != std::string::npos;
  }

  static std::unique_ptr<views::Label> MakeSectionLabel(
      std::string_view category) {
    auto label = std::make_unique<views::Label>(
        base::UTF8ToUTF16(category),
        views::Label::CustomFont{UiFont(11, gfx::Font::Weight::MEDIUM)});
    label->SetEnabledColor(kSecondaryText);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(4, 2, 0, 0)));
    return label;
  }

  static std::unique_ptr<views::View> MakeGridRow() {
    auto row = std::make_unique<views::View>();
    row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kGridGap));
    return row;
  }

  SkColor AccentColor() const {
    return ParseSpaceAccentColor(accent_color_, kPrimaryText);
  }

  void OnIconSelected(const std::string& icon_id) {
    icon_ = icon_id;
    for (IconGridButton* button : icon_buttons_) {
      button->SetSelected(button->icon_id() == icon_);
    }
    preview_->SetIcon(icon_);
  }

  void OnAccentSelected(const std::string& hex) {
    accent_color_ = hex;
    for (AccentSwatchButton* swatch : swatch_buttons_) {
      swatch->SetSelected(swatch->hex() == accent_color_);
    }
    for (IconGridButton* button : icon_buttons_) {
      button->SetAccent(AccentColor());
    }
    preview_->SetAccent(AccentColor());
  }

  raw_ptr<views::Textfield> name_field_ = nullptr;
  raw_ptr<views::Textfield> search_field_ = nullptr;
  raw_ptr<views::View> grid_container_ = nullptr;
  raw_ptr<SpaceIconPreview> preview_ = nullptr;
  std::vector<raw_ptr<IconGridButton>> icon_buttons_;
  std::vector<raw_ptr<AccentSwatchButton>> swatch_buttons_;

  std::string icon_;
  std::string accent_color_;
};

BEGIN_METADATA(SpaceEditorView)
END_METADATA

}  // namespace

void ShowSpaceEditorBubble(
    views::View* anchor_view,
    const std::u16string& title,
    const std::u16string& confirm_label,
    const SpaceEditorFields& initial_fields,
    base::OnceCallback<void(const SpaceEditorFields&)> on_accept) {
  if (!anchor_view) {
    return;
  }

  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor_view, views::BubbleBorder::BOTTOM_CENTER);
  delegate->SetTitle(title);
  delegate->SetShowTitle(true);
  delegate->SetShowCloseButton(false);
  delegate->SetBackgroundColor(kBubbleBg);
  delegate->set_margins(gfx::Insets::VH(12, 16));
  delegate->set_fixed_width(kContentWidth + 32);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
                       static_cast<int>(ui::mojom::DialogButton::kCancel));
  delegate->SetButtonLabel(ui::mojom::DialogButton::kOk, confirm_label);
  delegate->SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Cancel");

  auto* editor = delegate->SetContentsView(
      std::make_unique<SpaceEditorView>(initial_fields));

  delegate->SetAcceptCallback(base::BindOnce(
      [](SpaceEditorView* editor,
         base::OnceCallback<void(const SpaceEditorFields&)> callback) {
        if (callback) {
          std::move(callback).Run(editor->fields());
        }
      },
      base::Unretained(editor), std::move(on_accept)));

  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(delegate),
      views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  widget->Show();
  editor->FocusNameField();
}

}  // namespace avora
