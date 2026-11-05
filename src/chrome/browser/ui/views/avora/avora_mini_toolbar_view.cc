// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_mini_toolbar_view.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ref.h"
#include "base/strings/escape.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/avora/avora_space.h"
#include "chrome/browser/avora/avora_space_icons.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_lucide_icon.h"
#include "chrome/browser/ui/views/avora/avora_mini_window.h"
#include "chrome/browser/ui/views/avora/avora_sidebar_background.h"
#include "chrome/browser/ui/views/avora/avora_window_theme.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/url_formatter/url_formatter.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/ui_util.h"
#include "extensions/common/extension.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace avora {

namespace {

// Matches the sidebar's chrome so the mini window reads as the same browser:
// the window palette's sidebar fill, raised chips and text.  A disabled glyph
// is the text at this alpha.
constexpr SkAlpha kDisabledIconAlpha = 0x55;

gfx::FontList ChipFont() {
  return gfx::FontList({std::string("system-ui")}, gfx::Font::NORMAL, 13,
                       gfx::Font::Weight::NORMAL);
}

// The address chip: a rounded surface that reports clicks.  Same two-state
// idea as the sidebar's chip -- a label the user clicks to get a textfield.
class ChipView : public views::View {
  METADATA_HEADER(ChipView, views::View)

 public:
  explicit ChipView(base::RepeatingClosure callback)
      : callback_(std::move(callback)) {}

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    SetBackground(views::CreateRoundedRectBackground(
        WindowTheme::PaletteFor(this).raised,
        AvoraMiniToolbarView::kChipCornerRadius));
  }

  bool OnMousePressed(const ui::MouseEvent& event) override { return true; }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (HitTestPoint(event.location()) && callback_) {
      callback_.Run();
    }
  }

 private:
  base::RepeatingClosure callback_;
};

BEGIN_METADATA(ChipView)
END_METADATA

// The Space selector reads as one chip -- Space name and dropdown arrow
// together -- while still being two controls: naming the destination is the
// common case, and choosing a different one is the exception beside it.  The
// chip below draws the shared surface; its two children draw only their hover
// state, so the seam shows up under the cursor and nowhere else.
class SpaceChipView : public views::View {
  METADATA_HEADER(SpaceChipView, views::View)

 public:
  SpaceChipView() {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
    // Both halves stretch to the chip's full height, so their contents centre
    // on the same line rather than on their own preferred heights.
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    gfx::Size size = views::View::CalculatePreferredSize(available_size);
    size.set_height(AvoraMiniToolbarView::kChipHeight);
    size.set_width(
        std::min(size.width(), AvoraMiniToolbarView::kSpaceButtonMaxWidth));
    return size;
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    SetBackground(views::CreateRoundedRectBackground(
        WindowTheme::PaletteFor(this).raised,
        AvoraMiniToolbarView::kChipCornerRadius));
  }
};

BEGIN_METADATA(SpaceChipView)
END_METADATA

// Paints the hovered half of the chip.  Rounded on the chip's radius so the
// highlight follows the chip's outline instead of cutting a square out of it.
void PaintChipHover(gfx::Canvas* canvas, const views::Button& button) {
  if (button.GetState() != views::Button::STATE_HOVERED &&
      button.GetState() != views::Button::STATE_PRESSED) {
    return;
  }
  // The tab hover is the palette's nearest shade to the lift this used to
  // have (#3A414A against the default's #383E48).
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(WindowTheme::PaletteFor(&button).tab_hover);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawRoundRect(button.GetLocalBounds(),
                        AvoraMiniToolbarView::kChipCornerRadius, flags);
}

// The naming half: the destination Space's icon and name.
class SpaceButton : public views::LabelButton {
  METADATA_HEADER(SpaceButton, views::LabelButton)

 public:
  explicit SpaceButton(PressedCallback callback)
      : views::LabelButton(std::move(callback)) {
    label()->SetFontList(ChipFont());
    label()->SetElideBehavior(gfx::ELIDE_TAIL);
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, 10, 0, 6)));
    SetImageLabelSpacing(6);
    SetMinSize(gfx::Size(0, AvoraMiniToolbarView::kChipHeight));
  }

  void OnThemeChanged() override {
    views::LabelButton::OnThemeChanged();
    const SkColor text = WindowTheme::PaletteFor(this).text;
    SetTextColor(views::Button::STATE_NORMAL, text);
    SetTextColor(views::Button::STATE_HOVERED, text);
    SetTextColor(views::Button::STATE_PRESSED, text);
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    PaintChipHover(canvas, *this);
  }
};

BEGIN_METADATA(SpaceButton)
END_METADATA

// The choosing half: the dropdown arrow.
class SpaceMenuButton : public views::ImageButton {
  METADATA_HEADER(SpaceMenuButton, views::ImageButton)

 public:
  explicit SpaceMenuButton(PressedCallback callback)
      : views::ImageButton(std::move(callback)) {
    SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
    SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, 0, 0, 8)));
    SetTooltipText(u"Choose a Space");
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(AvoraMiniToolbarView::kSpaceMenuButtonWidth,
                     AvoraMiniToolbarView::kChipHeight);
  }

  void OnThemeChanged() override {
    views::ImageButton::OnThemeChanged();
    SetImageModel(views::Button::STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(
                      vector_icons::kCaretDownOldIcon,
                      WindowTheme::PaletteFor(this).text,
                      AvoraMiniToolbarView::kSpaceMenuIconSize));
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    PaintChipHover(canvas, *this);
  }
};

BEGIN_METADATA(SpaceMenuButton)
END_METADATA

// Runs a callback with the Space the user picked out of the selector menu.
class SpaceMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  SpaceMenuDelegate(std::vector<std::string> space_ids,
                    base::RepeatingCallback<void(const std::string&)> callback)
      : space_ids_(std::move(space_ids)), callback_(std::move(callback)) {}

  void ExecuteCommand(int command_id, int event_flags) override {
    const size_t index = static_cast<size_t>(command_id);
    if (index < space_ids_.size() && callback_) {
      callback_.Run(space_ids_[index]);
    }
  }

 private:
  std::vector<std::string> space_ids_;
  base::RepeatingCallback<void(const std::string&)> callback_;
};

// An image button showing |icon| in the palette's text colour, dimmed when
// disabled.  The glyph is re-coloured whenever the window's theme changes.
class GlyphButton : public views::ImageButton {
  METADATA_HEADER(GlyphButton, views::ImageButton)

 public:
  GlyphButton(PressedCallback callback, const gfx::VectorIcon& icon, int size)
      : views::ImageButton(std::move(callback)), icon_(icon), size_(size) {}

  void OnThemeChanged() override {
    views::ImageButton::OnThemeChanged();
    const SkColor text = WindowTheme::PaletteFor(this).text;
    SetImageModel(views::Button::STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(*icon_, text, size_));
    SetImageModel(views::Button::STATE_DISABLED,
                  ui::ImageModel::FromVectorIcon(
                      *icon_, SkColorSetA(text, kDisabledIconAlpha), size_));
  }

 private:
  const raw_ref<const gfx::VectorIcon> icon_;
  const int size_;
};

BEGIN_METADATA(GlyphButton)
END_METADATA

std::unique_ptr<views::ImageButton> MakeNavButton(
    const gfx::VectorIcon& icon,
    base::RepeatingClosure callback,
    const std::u16string& tooltip) {
  auto button = std::make_unique<GlyphButton>(
      std::move(callback), icon, AvoraMiniToolbarView::kNavIconSize);
  // ImageButton draws top-left by default, which puts the glyph above the
  // address text's centre line instead of on it.
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetPreferredSize(gfx::Size(AvoraMiniToolbarView::kNavButtonSize,
                                     AvoraMiniToolbarView::kNavButtonSize));
  button->SetTooltipText(tooltip);
  return button;
}

}  // namespace

AvoraMiniToolbarView::AvoraMiniToolbarView(
    BrowserWindowInterface* browser,
    NavigateAddressCallback navigate_address_cb)
    : browser_(browser),
      navigate_address_cb_(std::move(navigate_address_cb)) {
  BuildUI();
  if (browser_) {
    browser_->GetTabStripModel()->AddObserver(this);
  }
  // An extension can be installed, enabled or removed while the window is
  // open, and the row has to follow.
  if (Profile* const p = profile()) {
    if (auto* registry = extensions::ExtensionRegistry::Get(p)) {
      registry_observation_.Observe(registry);
    }
  }
  ObserveActiveContents();
  UpdateState();
}

AvoraMiniToolbarView::~AvoraMiniToolbarView() {
  if (browser_) {
    browser_->GetTabStripModel()->RemoveObserver(this);
  }
}

void AvoraMiniToolbarView::BuildUI() {
  SetBackground(CreateSidebarBackground());
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(0, kToolbarHPadding), kToolbarItemGap));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  back_button_ = AddChildView(MakeNavButton(
      vector_icons::kArrowBackIcon,
      base::BindRepeating(&AvoraMiniToolbarView::OnBackPressed,
                          base::Unretained(this)),
      u"Back"));
  forward_button_ = AddChildView(MakeNavButton(
      vector_icons::kArrowForwardIcon,
      base::BindRepeating(&AvoraMiniToolbarView::OnForwardPressed,
                          base::Unretained(this)),
      u"Forward"));
  reload_button_ = AddChildView(MakeNavButton(
      vector_icons::kRefreshIcon,
      base::BindRepeating(&AvoraMiniToolbarView::OnReloadPressed,
                          base::Unretained(this)),
      u"Reload"));

  auto chip = std::make_unique<ChipView>(base::BindRepeating(
      &AvoraMiniToolbarView::BeginEditingAddress, base::Unretained(this)));
  chip->SetPreferredSize(gfx::Size(0, kChipHeight));
  auto* chip_layout = chip->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 10)));
  chip_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto label = std::make_unique<views::Label>(
      std::u16string(), views::Label::CustomFont{ChipFont()});
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  address_label_ = chip->AddChildView(std::move(label));
  chip_layout->SetFlexForView(address_label_, 1);

  auto field = std::make_unique<views::Textfield>();
  field->SetFontList(ChipFont());
  field->SetBorder(nullptr);
  field->SetPlaceholderText(u"Search or enter URL");
  field->SetVisible(false);
  address_field_ = chip->AddChildView(std::move(field));
  address_field_->set_controller(this);
  chip_layout->SetFlexForView(address_field_, 1);

  address_chip_ = AddChildView(std::move(chip));
  layout->SetFlexForView(address_chip_, 1);

  // The extensions row sits between the page's address and the Space it would
  // be committed to: it belongs to the page, not to the destination.
  auto extensions_row = std::make_unique<views::View>();
  auto* extensions_layout =
      extensions_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          kExtensionGap));
  extensions_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  extensions_row_ = AddChildView(std::move(extensions_row));

  auto space_chip = std::make_unique<SpaceChipView>();
  space_button_ = space_chip->AddChildView(std::make_unique<SpaceButton>(
      base::BindRepeating(&AvoraMiniToolbarView::OnSpaceButtonPressed,
                          base::Unretained(this))));
  // The picker commits to the Space it names, so aiming somewhere else needs
  // its own control rather than a second meaning for the same click.
  space_menu_button_ = space_chip->AddChildView(
      std::make_unique<SpaceMenuButton>(base::BindRepeating(
          &AvoraMiniToolbarView::ShowSpaceMenu, base::Unretained(this))));
  space_chip_ = AddChildView(std::move(space_chip));
}

gfx::Size AvoraMiniToolbarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  gfx::Size size = views::View::CalculatePreferredSize(available_size);
  size.set_height(kToolbarHeight);
  return size;
}

void AvoraMiniToolbarView::OnThemeChanged() {
  views::View::OnThemeChanged();
  // Children hear of the change first, so the textfield has already reset its
  // text colour from Chromium's colour provider; this puts the palette's back.
  const SpaceThemePalette& palette = WindowTheme::PaletteFor(this);
  address_label_->SetEnabledColor(palette.text);
  address_field_->SetColor(palette.text);
  address_field_->SetBackgroundColor(palette.raised);
  // The Space button's glyph, when there is no Space to name, is drawn in the
  // palette's text colour.
  UpdateSpaceButton();
}

void AvoraMiniToolbarView::UpdateState() {
  UpdateAddress();
  UpdateNavButtons();
  UpdateExtensionIcons();
  UpdateSpaceButton();
}

Profile* AvoraMiniToolbarView::profile() const {
  return browser_ ? browser_->GetProfile() : nullptr;
}

content::WebContents* AvoraMiniToolbarView::active_contents() const {
  return browser_ ? browser_->GetTabStripModel()->GetActiveWebContents()
                  : nullptr;
}

void AvoraMiniToolbarView::ObserveActiveContents() {
  Observe(active_contents());
}

// ── Nav row ─────────────────────────────────────────────────────────────────

void AvoraMiniToolbarView::OnBackPressed() {
  if (browser_) {
    chrome::ExecuteCommand(browser_, IDC_BACK);
  }
}

void AvoraMiniToolbarView::OnForwardPressed() {
  if (browser_) {
    chrome::ExecuteCommand(browser_, IDC_FORWARD);
  }
}

void AvoraMiniToolbarView::OnReloadPressed() {
  if (browser_) {
    chrome::ExecuteCommand(browser_, IDC_RELOAD);
  }
}

void AvoraMiniToolbarView::UpdateNavButtons() {
  content::WebContents* contents = active_contents();
  if (!contents || !back_button_) {
    return;
  }
  back_button_->SetEnabled(contents->GetController().CanGoBack());
  forward_button_->SetEnabled(contents->GetController().CanGoForward());
}

// ── Address bar ─────────────────────────────────────────────────────────────

void AvoraMiniToolbarView::BeginEditingAddress() {
  content::WebContents* contents = active_contents();
  if (!address_field_ || !address_label_ || !contents) {
    return;
  }
  // Already editing (Cmd-L pressed again): reselect what the user has typed
  // rather than throwing it away for the page's URL.
  if (address_field_->GetVisible() && address_field_->HasFocus()) {
    address_field_->SelectAll(false);
    return;
  }
  const GURL url = contents->GetVisibleURL();
  address_field_->SetText(url.is_valid() ? base::UTF8ToUTF16(url.spec())
                                         : std::u16string());
  address_label_->SetVisible(false);
  address_field_->SetVisible(true);
  address_field_->RequestFocus();
  address_field_->SelectAll(false);
  DeprecatedLayoutImmediately();
}

void AvoraMiniToolbarView::EndEditingAddress() {
  if (!address_field_ || !address_label_) {
    return;
  }
  address_field_->SetVisible(false);
  address_label_->SetVisible(true);
  if (content::WebContents* contents = active_contents()) {
    contents->Focus();
  }
  DeprecatedLayoutImmediately();
}

void AvoraMiniToolbarView::UpdateAddress() {
  if (!address_label_) {
    return;
  }
  content::WebContents* contents = active_contents();
  const GURL url = contents ? contents->GetVisibleURL() : GURL();
  address_label_->SetText(
      url.is_valid() ? url_formatter::FormatUrl(
                           url, url_formatter::kFormatUrlOmitDefaults,
                           base::UnescapeRule::SPACES, nullptr, nullptr,
                           nullptr)
                     : std::u16string());
}

bool AvoraMiniToolbarView::HandleKeyEvent(views::Textfield* sender,
                                          const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    EndEditingAddress();
    UpdateAddress();
    return true;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    const std::u16string text(sender->GetText());
    EndEditingAddress();
    if (navigate_address_cb_) {
      navigate_address_cb_.Run(text);
    }
    UpdateAddress();
    return true;
  }
  return false;
}

// ── Extensions row ──────────────────────────────────────────────────────────

void AvoraMiniToolbarView::UpdateExtensionIcons() {
  Profile* const p = profile();
  if (!extensions_row_ || !p) {
    return;
  }

  extension_buttons_.clear();
  extensions_row_->RemoveAllChildViews();

  auto* registry = extensions::ExtensionRegistry::Get(p);
  auto* action_manager = extensions::ExtensionActionManager::Get(p);
  if (!registry || !action_manager) {
    extensions_row_->SetVisible(false);
    return;
  }

  for (const auto& extension : registry->enabled_extensions()) {
    if (!extension->is_extension() ||
        !extensions::ui_util::ShouldDisplayInExtensionSettings(*extension)) {
      continue;
    }
    extensions::ExtensionAction* action =
        action_manager->GetExtensionAction(*extension);
    if (!action) {
      continue;
    }

    // The icon the extension already has loaded for its action -- the same
    // source the main window's row uses, and synchronous, so the row never
    // flashes empty.  Extensions without one get the puzzle piece, which a
    // GlyphButton keeps in the palette's text colour.
    const gfx::Image icon = action->GetDefaultIconImage();
    auto callback = base::BindRepeating(
        &AvoraMiniToolbarView::OnExtensionIconPressed, base::Unretained(this),
        extension->id());
    std::unique_ptr<views::ImageButton> button;
    if (!icon.IsEmpty()) {
      button = std::make_unique<views::ImageButton>(std::move(callback));
      button->SetImageModel(views::Button::STATE_NORMAL,
                            ui::ImageModel::FromImage(icon));
    } else {
      button = std::make_unique<GlyphButton>(
          std::move(callback), vector_icons::kChromeExtensionIcon,
          kExtensionIconSize);
    }
    button->SetPreferredSize(
        gfx::Size(kExtensionButtonSize, kExtensionButtonSize));
    button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
    button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    button->SetTooltipText(base::UTF8ToUTF16(extension->name()));
    button->SetAccessibleName(base::UTF8ToUTF16(extension->name()));

    extension_buttons_[extension->id()] =
        extensions_row_->AddChildView(std::move(button));
  }

  extensions_row_->SetVisible(!extension_buttons_.empty());
}

void AvoraMiniToolbarView::OnExtensionIconPressed(
    const std::string& extension_id) {
  // Shown through the same path the main window uses, anchored to the icon the
  // user actually clicked.
  if (BrowserView* const browser_view =
          BrowserView::GetBrowserViewForBrowser(browser_)) {
    browser_view->ShowAvoraExtensionPopup(
        extension_id, ExtensionAnchor(extension_id), ShowPopupCallback());
  }
}

views::View* AvoraMiniToolbarView::ExtensionAnchor(
    const std::string& extension_id) {
  const auto it = extension_buttons_.find(extension_id);
  return it == extension_buttons_.end() ? nullptr : it->second.get();
}

void AvoraMiniToolbarView::OnExtensionLoaded(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension) {
  UpdateExtensionIcons();
}

void AvoraMiniToolbarView::OnExtensionUnloaded(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension,
    extensions::UnloadedExtensionReason reason) {
  UpdateExtensionIcons();
}

// ── Space selector ──────────────────────────────────────────────────────────

void AvoraMiniToolbarView::UpdateSpaceButton() {
  if (!space_button_ || !profile() || !profile()->GetPrefs()) {
    return;
  }
  const std::vector<Space> spaces = MiniWindowOrderedSpaces(browser_);
  if (spaces.empty()) {
    // A profile with no Spaces yet still needs a way out of the mini window,
    // so the button commits the link untagged rather than disappearing.
    space_button_->SetText(u"Open Tab");
    space_button_->SetImageModel(
        views::Button::STATE_NORMAL,
        LucideIconImageModel(kDefaultSpaceIconId, kSpaceIconSize,
                             WindowTheme::PaletteFor(this).text));
    space_button_->SetTooltipText(u"Open in a tab");
    if (space_menu_button_) {
      space_menu_button_->SetVisible(false);
    }
    return;
  }
  const Space& target = spaces.front();
  space_button_->SetText(base::UTF8ToUTF16(target.name));
  space_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      LucideIconImageModel(NormalizeSpaceIconId(target.icon), kSpaceIconSize,
                           target.AccentColor()));
  space_button_->SetTooltipText(u"Open in " + base::UTF8ToUTF16(target.name));
  if (space_menu_button_) {
    // Only one Space to offer means nothing to choose between.
    space_menu_button_->SetVisible(spaces.size() > 1);
  }
}

void AvoraMiniToolbarView::OnSpaceButtonPressed() {
  const std::vector<Space> spaces = MiniWindowOrderedSpaces(browser_);
  SendMiniWindowToSpace(browser_, spaces.empty() ? std::string() : spaces.front().id);
}

void AvoraMiniToolbarView::ShowSpaceMenu() {
  const std::vector<Space> spaces = MiniWindowOrderedSpaces(browser_);
  if (spaces.size() < 2) {
    return;
  }

  std::vector<std::string> ids;
  ids.reserve(spaces.size());
  for (const Space& space : spaces) {
    ids.push_back(space.id);
  }

  // The menu outlives this call, so the callback holds a weak pointer: the
  // window can be closed while the menu is up.
  space_menu_delegate_ = std::make_unique<SpaceMenuDelegate>(
      std::move(ids),
      base::BindRepeating(
          [](base::WeakPtr<AvoraMiniToolbarView> toolbar,
             const std::string& space_id) {
            if (toolbar) {
              SendMiniWindowToSpace(toolbar->browser_, space_id);
            }
          },
          weak_factory_.GetWeakPtr()));
  space_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(space_menu_delegate_.get());
  for (size_t i = 0; i < spaces.size(); ++i) {
    space_menu_model_->AddItemWithIcon(
        static_cast<int>(i), base::UTF8ToUTF16(spaces[i].name),
        LucideIconImageModel(NormalizeSpaceIconId(spaces[i].icon),
                             kSpaceIconSize, spaces[i].AccentColor()));
  }

  space_menu_runner_ = std::make_unique<views::MenuRunner>(
      space_menu_model_.get(), views::MenuRunner::HAS_MNEMONICS);
  space_menu_runner_->RunMenuAt(
      GetWidget(), nullptr, space_menu_button_->GetBoundsInScreen(),
      views::MenuAnchorPosition::kTopRight, ui::mojom::MenuSourceType::kMouse);
}

// ── TabStripModelObserver ───────────────────────────────────────────────────

void AvoraMiniToolbarView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // The mini window holds one tab, but it arrives after the window does and
  // is replaced when the window is re-pointed at another Space.
  ObserveActiveContents();
  UpdateState();
}

// ── content::WebContentsObserver ────────────────────────────────────────────

void AvoraMiniToolbarView::DidStartLoading() {
  UpdateAddress();
  UpdateNavButtons();
}

void AvoraMiniToolbarView::DidStopLoading() {
  UpdateAddress();
  UpdateNavButtons();
}

void AvoraMiniToolbarView::NavigationEntryCommitted(
    const content::LoadCommittedDetails& load_details) {
  UpdateAddress();
  UpdateNavButtons();
}

BEGIN_METADATA(AvoraMiniToolbarView)
END_METADATA

}  // namespace avora
