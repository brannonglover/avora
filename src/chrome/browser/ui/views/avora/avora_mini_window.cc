// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_mini_window.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/escape.h"
#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/avora/avora_search_engine.h"
#include "chrome/browser/avora/avora_space_icons.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_tab_site_instance.h"
#include "chrome/browser/avora/avora_tab_space.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tab_helpers.h"
#include "chrome/browser/ui/views/avora/avora_lucide_icon.h"
#include "chrome/browser/ui/views/avora/avora_toast_view.h"
#include "ui/base/base_window.h"
#include "components/prefs/pref_service.h"
#include "components/url_formatter/url_formatter.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/models/image_model.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/base/window_open_disposition.h"
#include "ui/display/screen.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

// The mini window hosts an ordinary page, but its WebContents never enters a
// TabStripModel, so none of Chromium's usual adoption points ever run for it.
// Attaching the tab helpers by hand is what gives the page a session tab ID
// and a ChromeExtensionWebContentsObserver, the same reasoning as the link
// preview's attacher in browser_view.cc.
//
// This class exists solely to hold the friendship declared in tab_helpers.h.
class AvoraMiniWindowTabHelperAttacher {
 public:
  static void Attach(content::WebContents* contents) {
    TabHelpers::AttachTabHelpers(contents);
  }
};

namespace avora {

namespace {

// Matches the sidebar's chrome so the mini window reads as the same browser.
constexpr SkColor kToolbarBg = SkColorSetRGB(0x18, 0x1C, 0x20);
constexpr SkColor kIconColor = SkColorSetRGB(0xED, 0xF2, 0xF5);
constexpr SkColor kChipBg = SkColorSetRGB(0x32, 0x38, 0x40);
constexpr SkColor kChipHoverBg = SkColorSetRGB(0x3A, 0x41, 0x4A);
constexpr SkColor kDisabledIconColor = SkColorSetARGB(0x55, 0xED, 0xF2, 0xF5);

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
      : callback_(std::move(callback)) {
    SetBackground(views::CreateRoundedRectBackground(
        kChipBg, MiniWindowView::kChipCornerRadius));
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
    SetBackground(views::CreateRoundedRectBackground(
        kChipBg, MiniWindowView::kChipCornerRadius));
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
    size.set_height(MiniWindowView::kChipHeight);
    size.set_width(std::min(size.width(), MiniWindowView::kSpaceButtonMaxWidth));
    return size;
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
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(kChipHoverBg);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawRoundRect(button.GetLocalBounds(),
                        MiniWindowView::kChipCornerRadius, flags);
}

// The naming half: the destination Space's icon and name.
class SpaceButton : public views::LabelButton {
  METADATA_HEADER(SpaceButton, views::LabelButton)

 public:
  explicit SpaceButton(PressedCallback callback)
      : views::LabelButton(std::move(callback)) {
    SetTextColor(views::Button::STATE_NORMAL, kIconColor);
    SetTextColor(views::Button::STATE_HOVERED, kIconColor);
    SetTextColor(views::Button::STATE_PRESSED, kIconColor);
    label()->SetFontList(ChipFont());
    label()->SetElideBehavior(gfx::ELIDE_TAIL);
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, 10, 0, 6)));
    SetImageLabelSpacing(6);
    SetMinSize(gfx::Size(0, MiniWindowView::kChipHeight));
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
    SetImageModel(views::Button::STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(
                      vector_icons::kCaretDownOldIcon, kIconColor,
                      MiniWindowView::kSpaceMenuIconSize));
    SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
    SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, 0, 0, 8)));
    SetTooltipText(u"Choose a Space");
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(MiniWindowView::kSpaceMenuButtonWidth,
                     MiniWindowView::kChipHeight);
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

std::unique_ptr<views::ImageButton> MakeNavButton(
    const gfx::VectorIcon& icon,
    base::RepeatingClosure callback,
    const std::u16string& tooltip) {
  auto button = std::make_unique<views::ImageButton>(std::move(callback));
  // ImageButton draws top-left by default, which puts the glyph above the
  // address text's centre line instead of on it.
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(icon, kIconColor,
                                     MiniWindowView::kNavIconSize));
  button->SetImageModel(
      views::Button::STATE_DISABLED,
      ui::ImageModel::FromVectorIcon(icon, kDisabledIconColor,
                                     MiniWindowView::kNavIconSize));
  button->SetPreferredSize(
      gfx::Size(MiniWindowView::kNavButtonSize,
                MiniWindowView::kNavButtonSize));
  button->SetTooltipText(tooltip);
  return button;
}

// ── The window itself ───────────────────────────────────────────────────────

// Owns the mini window's widget.  At most one exists: a second external link
// re-points the open window rather than stacking another one on screen.
class MiniWindowHost {
 public:
  MiniWindowHost(Profile* profile, const GURL& url) {
    delegate_ = std::make_unique<views::WidgetDelegate>();
    delegate_->SetCanResize(true);
    delegate_->SetCanMaximize(false);
    delegate_->SetCanMinimize(true);
    delegate_->SetShowTitle(true);
    delegate_->SetTitle(u"Avora");
    view_ = delegate_->SetContentsView(
        std::make_unique<MiniWindowView>(profile, url));

    views::Widget::InitParams params(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_WINDOW);
    params.delegate = delegate_.get();
    params.name = "AvoraMiniWindow";
    params.bounds = InitialBounds();

    widget_ = std::make_unique<views::Widget>();
    widget_->Init(std::move(params));
    widget_->MakeCloseSynchronous(base::BindOnce(
        &MiniWindowHost::OnCloseRequested, base::Unretained(this)));
  }

  MiniWindowHost(const MiniWindowHost&) = delete;
  MiniWindowHost& operator=(const MiniWindowHost&) = delete;
  ~MiniWindowHost() = default;

  void ShowAndFocus() {
    if (!widget_) {
      return;
    }
    widget_->Show();
    widget_->Activate();
  }

  void NavigateTo(const GURL& url) {
    if (view_) {
      view_->NavigateTo(url);
    }
  }

  // Tears the window down on the next turn of the loop.  Callers reach here
  // from inside the view (the Space selector) and from inside the widget (the
  // close button), neither of which can survive having its owner deleted
  // underneath it.
  void CloseSoon() {
    if (closing_) {
      return;
    }
    closing_ = true;
    if (widget_) {
      widget_->Hide();
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                                 this);
  }

 private:
  static gfx::Rect InitialBounds() {
    gfx::Rect bounds(0, 0, MiniWindowView::kDefaultWidth,
                     MiniWindowView::kDefaultHeight);
    display::Screen* screen = display::Screen::Get();
    if (!screen) {
      return bounds;
    }
    // On the display the user is actually looking at, which for a link handed
    // over by another app is the one that app was on.
    const gfx::Rect work_area =
        screen->GetDisplayNearestPoint(screen->GetCursorScreenPoint())
            .work_area();
    bounds.set_size(
        gfx::Size(std::min(bounds.width(), work_area.width()),
                  std::min(bounds.height(), work_area.height())));
    // Slightly above centre: a tall window centred exactly reads as low.
    bounds.set_origin(
        gfx::Point(work_area.x() + (work_area.width() - bounds.width()) / 2,
                   work_area.y() + (work_area.height() - bounds.height()) / 3));
    return bounds;
  }

  void OnCloseRequested(views::Widget::ClosedReason reason);

  // Declared before |widget_| so the widget -- and with it the contents view --
  // is destroyed while the delegate is still alive.
  std::unique_ptr<views::WidgetDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<MiniWindowView> view_ = nullptr;
  bool closing_ = false;
};

MiniWindowHost* g_mini_window = nullptr;

void MiniWindowHost::OnCloseRequested(views::Widget::ClosedReason reason) {
  if (g_mini_window == this) {
    g_mini_window = nullptr;
  }
  CloseSoon();
}

// Creates the page the mini window shows.  It loads in |space_id|'s storage
// partition, so a link opened while the user was last in their work Space
// arrives signed in as that Space rather than as a stranger.
std::unique_ptr<content::WebContents> CreateMiniWindowContents(
    Profile* profile,
    const std::string& space_id,
    const GURL& url) {
  return content::WebContents::Create(content::WebContents::CreateParams(
      profile, GetSiteInstanceForSpace(profile, space_id, url)));
}

}  // namespace

// ── Public entry points ─────────────────────────────────────────────────────

void ShowMiniWindow(Profile* profile, const GURL& url) {
  if (!profile || !url.is_valid()) {
    return;
  }
  if (g_mini_window) {
    g_mini_window->NavigateTo(url);
    g_mini_window->ShowAndFocus();
    return;
  }
  g_mini_window = new MiniWindowHost(profile, url);
  g_mini_window->ShowAndFocus();
}

bool IsMiniWindowShowing() {
  return g_mini_window != nullptr;
}

void CloseMiniWindow() {
  if (!g_mini_window) {
    return;
  }
  MiniWindowHost* host = g_mini_window;
  g_mini_window = nullptr;
  host->CloseSoon();
}

std::vector<Space> OrderMiniWindowSpaces(const std::vector<Space>& spaces,
                                         const std::string& preferred) {
  std::vector<Space> ordered;
  ordered.reserve(spaces.size());
  for (const Space& space : spaces) {
    if (!preferred.empty() && space.id == preferred) {
      ordered.push_back(space);
    }
  }
  for (const Space& space : spaces) {
    if (preferred.empty() || space.id != preferred) {
      ordered.push_back(space);
    }
  }
  return ordered;
}

// ── MiniWindowView ──────────────────────────────────────────────────────────

MiniWindowView::MiniWindowView(Profile* profile, const GURL& url)
    : profile_(profile) {
  contents_ = CreateMiniWindowContents(profile_, PreferredSpaceId(), url);
  contents_->SetDelegate(this);
  // Before the first navigation, so the helpers observe the load from the
  // start rather than joining a page that is already committed.
  AvoraMiniWindowTabHelperAttacher::Attach(contents_.get());
  Observe(contents_.get());

  BuildUI();

  // Cmd-Shift-C copies the page's URL here exactly as it does in the main
  // window.  The main window's binding runs through the Edit menu against a
  // Browser's active tab, which a mini window has neither of, so it carries
  // its own.
  AddAccelerator(ui::Accelerator(
      ui::VKEY_C, ui::EF_PLATFORM_ACCELERATOR | ui::EF_SHIFT_DOWN));

  web_view_->SetWebContents(contents_.get());
  NavigateTo(url);
}

MiniWindowView::~MiniWindowView() {
  Observe(nullptr);
  if (web_view_) {
    web_view_->SetWebContents(nullptr);
  }
  if (contents_) {
    contents_->SetDelegate(nullptr);
  }
}

void MiniWindowView::BuildUI() {
  SetBackground(views::CreateSolidBackground(kToolbarBg));
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // --- Toolbar: nav buttons, address chip, Space selector ---
  auto* toolbar = AddChildView(std::make_unique<views::View>());
  toolbar->SetPreferredSize(gfx::Size(0, kToolbarHeight));
  auto* toolbar_layout =
      toolbar->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, kToolbarHPadding), kToolbarItemGap));
  toolbar_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  back_button_ = toolbar->AddChildView(MakeNavButton(
      vector_icons::kArrowBackIcon,
      base::BindRepeating(&MiniWindowView::OnBackPressed,
                          base::Unretained(this)),
      u"Back"));
  forward_button_ = toolbar->AddChildView(MakeNavButton(
      vector_icons::kArrowForwardIcon,
      base::BindRepeating(&MiniWindowView::OnForwardPressed,
                          base::Unretained(this)),
      u"Forward"));
  reload_button_ = toolbar->AddChildView(MakeNavButton(
      vector_icons::kRefreshIcon,
      base::BindRepeating(&MiniWindowView::OnReloadPressed,
                          base::Unretained(this)),
      u"Reload"));

  auto chip = std::make_unique<ChipView>(base::BindRepeating(
      &MiniWindowView::BeginEditingAddress, base::Unretained(this)));
  chip->SetPreferredSize(gfx::Size(0, kChipHeight));
  auto* chip_layout = chip->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 10)));
  chip_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto label = std::make_unique<views::Label>(
      std::u16string(), views::Label::CustomFont{ChipFont()});
  label->SetEnabledColor(kIconColor);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  address_label_ = chip->AddChildView(std::move(label));
  chip_layout->SetFlexForView(address_label_, 1);

  auto field = std::make_unique<views::Textfield>();
  field->SetFontList(ChipFont());
  field->SetColor(kIconColor);
  field->SetBackgroundColor(kChipBg);
  field->SetBorder(nullptr);
  field->SetPlaceholderText(u"Search or enter URL");
  field->SetVisible(false);
  address_field_ = chip->AddChildView(std::move(field));
  address_field_->set_controller(this);
  chip_layout->SetFlexForView(address_field_, 1);

  address_chip_ = toolbar->AddChildView(std::move(chip));
  toolbar_layout->SetFlexForView(address_chip_, 1);

  auto space_chip = std::make_unique<SpaceChipView>();
  space_button_ = space_chip->AddChildView(std::make_unique<SpaceButton>(
      base::BindRepeating(&MiniWindowView::OnSpaceButtonPressed,
                          base::Unretained(this))));
  // The picker commits to the Space it names, so aiming somewhere else needs
  // its own control rather than a second meaning for the same click.
  space_menu_button_ = space_chip->AddChildView(
      std::make_unique<SpaceMenuButton>(base::BindRepeating(
          &MiniWindowView::ShowSpaceMenu, base::Unretained(this))));
  space_chip_ = toolbar->AddChildView(std::move(space_chip));

  // --- The page ---
  web_view_ = AddChildView(std::make_unique<views::WebView>(profile_));
  layout->SetFlexForView(web_view_, 1);

  // The toast floats over the page in the upper-right, so it is positioned by
  // hand and kept out of the vertical stack.
  toast_ = AddChildView(std::make_unique<AvoraToastView>());
  toast_->SetProperty(views::kViewIgnoredByLayoutKey, true);
  toast_->SetVisible(false);

  UpdateNavButtons();
  UpdateSpaceButton();
}

void MiniWindowView::NavigateTo(const GURL& url) {
  if (!contents_ || !url.is_valid()) {
    return;
  }
  contents_->GetController().LoadURL(url, content::Referrer(),
                                     ui::PAGE_TRANSITION_LINK, std::string());
  UpdateAddress();
  UpdateNavButtons();
}

GURL MiniWindowView::current_url() const {
  return contents_ ? contents_->GetVisibleURL() : GURL();
}

// ── Nav row ─────────────────────────────────────────────────────────────────

void MiniWindowView::OnBackPressed() {
  if (contents_ && contents_->GetController().CanGoBack()) {
    contents_->GetController().GoBack();
  }
}

void MiniWindowView::OnForwardPressed() {
  if (contents_ && contents_->GetController().CanGoForward()) {
    contents_->GetController().GoForward();
  }
}

void MiniWindowView::OnReloadPressed() {
  if (contents_) {
    contents_->GetController().Reload(content::ReloadType::NORMAL, false);
  }
}

void MiniWindowView::UpdateNavButtons() {
  if (!contents_ || !back_button_) {
    return;
  }
  back_button_->SetEnabled(contents_->GetController().CanGoBack());
  forward_button_->SetEnabled(contents_->GetController().CanGoForward());
}

// ── Address bar ─────────────────────────────────────────────────────────────

void MiniWindowView::BeginEditingAddress() {
  if (!address_field_ || !address_label_) {
    return;
  }
  const GURL url = current_url();
  address_field_->SetText(url.is_valid() ? base::UTF8ToUTF16(url.spec())
                                         : std::u16string());
  address_label_->SetVisible(false);
  address_field_->SetVisible(true);
  address_field_->RequestFocus();
  address_field_->SelectAll(false);
  DeprecatedLayoutImmediately();
}

void MiniWindowView::EndEditingAddress() {
  if (!address_field_ || !address_label_) {
    return;
  }
  address_field_->SetVisible(false);
  address_label_->SetVisible(true);
  if (web_view_) {
    web_view_->RequestFocus();
  }
  DeprecatedLayoutImmediately();
}

void MiniWindowView::UpdateAddress() {
  if (!address_label_) {
    return;
  }
  const GURL url = current_url();
  address_label_->SetText(
      url.is_valid() ? url_formatter::FormatUrl(
                           url, url_formatter::kFormatUrlOmitDefaults,
                           base::UnescapeRule::SPACES, nullptr, nullptr,
                           nullptr)
                     : std::u16string());
}

void MiniWindowView::UpdateWindowTitle() {
  views::Widget* widget = GetWidget();
  if (!widget || !widget->widget_delegate() || !contents_) {
    return;
  }
  const std::u16string title = contents_->GetTitle();
  widget->widget_delegate()->SetTitle(title.empty() ? u"Avora" : title);
}

bool MiniWindowView::HandleKeyEvent(views::Textfield* sender,
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
    const GURL url =
        ResolveAddressInput(profile_->GetPrefs(), sender->GetText());
    EndEditingAddress();
    if (url.is_valid()) {
      NavigateTo(url);
    } else {
      UpdateAddress();
    }
    return true;
  }
  return false;
}

// ── Copy URL ────────────────────────────────────────────────────────────────

void MiniWindowView::CopyUrlToClipboard() {
  const GURL url = current_url();
  if (!url.is_valid()) {
    return;
  }
  ui::ScopedClipboardWriter scw(ui::ClipboardBuffer::kCopyPaste);
  scw.WriteText(base::UTF8ToUTF16(url.spec()));
  // Same wording as chrome::CopyURL, so the confirmation reads identically
  // whichever window the user copied from.
  ShowToast(u"Copied URL");
}

void MiniWindowView::ShowToast(const std::u16string& message) {
  if (!toast_ || !web_view_) {
    return;
  }
  toast_->Show(message, vector_icons::kLinkIcon);

  // Upper-right of the page area.  The toast reserves room around its card for
  // the drop shadow, so this margin is on top of that reserved space -- the
  // same arrangement the main window's layout uses.
  constexpr int kToastMargin = 2;
  const gfx::Size toast_size = toast_->GetPreferredSize({});
  const gfx::Rect page = web_view_->bounds();
  toast_->SetBounds(page.right() - kToastMargin - toast_size.width(),
                    page.y() + kToastMargin, toast_size.width(),
                    toast_size.height());
  // Above the page, which is a native view host and would otherwise cover it.
  ReorderChildView(toast_, children().size());
}

bool MiniWindowView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  if (accelerator.key_code() == ui::VKEY_C &&
      accelerator.modifiers() ==
          (ui::EF_PLATFORM_ACCELERATOR | ui::EF_SHIFT_DOWN)) {
    CopyUrlToClipboard();
    return true;
  }
  return false;
}

bool MiniWindowView::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  // The page had first refusal; anything it did not take is offered to this
  // window's accelerators.
  return unhandled_key_handler_.HandleKeyboardEvent(event, GetFocusManager());
}

// ── Space selector ──────────────────────────────────────────────────────────

std::string MiniWindowView::PreferredSpaceId() const {
  if (!profile_ || !profile_->GetPrefs()) {
    return std::string();
  }
  // The window the user was last in is the likeliest home for the link, so its
  // Space -- not the profile-wide one -- is what the selector offers first.
  if (BrowserWindowInterface* browser =
          ProfileBrowserCollection::GetForProfile(profile_)
              ->FindTabbedBrowser()) {
    if (WindowSpaceState* state = GetWindowSpaceState(browser);
        state && !state->active_space_id().empty()) {
      return state->active_space_id();
    }
  }
  SpaceManager manager(profile_->GetPrefs());
  const Space* active = manager.GetActiveSpace();
  return active ? active->id : std::string();
}

std::vector<Space> MiniWindowView::OrderedSpaces() const {
  if (!profile_ || !profile_->GetPrefs()) {
    return {};
  }
  SpaceManager manager(profile_->GetPrefs());
  return OrderMiniWindowSpaces(manager.GetSpaces(), PreferredSpaceId());
}

void MiniWindowView::UpdateSpaceButton() {
  if (!space_button_) {
    return;
  }
  const std::vector<Space> spaces = OrderedSpaces();
  if (spaces.empty()) {
    // A profile with no Spaces yet still needs a way out of the mini window,
    // so the button commits the link untagged rather than disappearing.
    space_button_->SetText(u"Open Tab");
    space_button_->SetImageModel(
        views::Button::STATE_NORMAL,
        LucideIconImageModel(kDefaultSpaceIconId, kSpaceIconSize, kIconColor));
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
    space_menu_button_->SetVisible(spaces.size() > 1);
  }
}

void MiniWindowView::OnSpaceButtonPressed() {
  // The button names its destination, so the click is the commitment -- no
  // menu in the way of the common case.
  const std::vector<Space> spaces = OrderedSpaces();
  SendToSpace(spaces.empty() ? std::string() : spaces.front().id);
}

void MiniWindowView::ShowSpaceMenu() {
  const std::vector<Space> spaces = OrderedSpaces();
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
      std::move(ids), base::BindRepeating(&MiniWindowView::SendToSpace,
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

void MiniWindowView::SendToSpace(const std::string& space_id) {
  const GURL url = current_url();
  if (!url.is_valid() || !profile_) {
    return;
  }

  BrowserWindowInterface* browser =
      ProfileBrowserCollection::GetForProfile(profile_)->FindTabbedBrowser();
  if (!browser) {
    // Nothing to receive the tab: a mini window can be the only Avora window
    // on screen, and this is the moment the user asked for a real one.
    browser = CreateBrowserWindow(
        BrowserWindowCreateParams(profile_.get(), /*from_user_gesture=*/true));
    if (!browser) {
      return;
    }
    browser->GetWindow()->Show();
  }

  // Point the window at the Space before navigating.  A new tab's storage
  // partition is resolved from the window's Space, so switching afterwards
  // would put the tab in the wrong identity -- and the user would land on a
  // Space that isn't showing their new tab.
  if (!space_id.empty()) {
    if (WindowSpaceState* state = GetWindowSpaceState(browser)) {
      state->SetActiveSpaceId(space_id);
    }
  }

  NavigateParams params(browser, url, ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
  if (params.navigated_or_inserted_contents && !space_id.empty()) {
    // Tag it so the Space's daily list claims the tab even if the window is
    // switched away before the load finishes.
    SetTabSpaceId(params.navigated_or_inserted_contents, space_id);
  }

  browser->GetWindow()->Activate();
  CloseMiniWindow();
}

// ── content::WebContentsDelegate ────────────────────────────────────────────

content::WebContents* MiniWindowView::OpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params,
    base::OnceCallback<void(content::NavigationHandle&)>
        navigation_handle_callback) {
  if (!contents_) {
    return nullptr;
  }
  // Every navigation the page asks for stays in this window -- it has no tabs
  // to open one in, and spawning a browser window behind the user's back is
  // exactly what the mini window exists to avoid.
  content::NavigationController::LoadURLParams load_params(params.url);
  load_params.referrer = params.referrer;
  load_params.transition_type = params.transition;
  load_params.extra_headers = params.extra_headers;
  load_params.is_renderer_initiated = params.is_renderer_initiated;
  load_params.initiator_origin = params.initiator_origin;
  load_params.source_site_instance = params.source_site_instance;

  base::WeakPtr<content::NavigationHandle> handle =
      contents_->GetController().LoadURLWithParams(load_params);
  if (navigation_handle_callback && handle) {
    std::move(navigation_handle_callback).Run(*handle);
  }
  return contents_.get();
}

content::WebContents* MiniWindowView::AddNewContents(
    content::WebContents* source,
    std::unique_ptr<content::WebContents> new_contents,
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features,
    bool user_gesture,
    bool* was_blocked) {
  // A target="_blank" link inside the mini window follows in place.  The
  // contents handed over here is dropped rather than adopted: the mini window
  // shows one page, and re-navigating is indistinguishable to the user for
  // links that carry no renderer state yet.
  if (target_url.is_valid()) {
    NavigateTo(target_url);
  }
  return nullptr;
}

void MiniWindowView::NavigationStateChanged(
    content::WebContents* source,
    content::InvalidateTypes changed_flags) {
  if (changed_flags & content::INVALIDATE_TYPE_URL) {
    UpdateAddress();
  }
  if (changed_flags & content::INVALIDATE_TYPE_TITLE) {
    UpdateWindowTitle();
  }
  if (changed_flags & content::INVALIDATE_TYPE_LOAD) {
    UpdateNavButtons();
  }
}

void MiniWindowView::LoadingStateChanged(content::WebContents* source,
                                         bool should_show_loading_ui) {
  UpdateNavButtons();
}

void MiniWindowView::CloseContents(content::WebContents* source) {
  // window.close() from the page.
  CloseMiniWindow();
}

// ── content::WebContentsObserver ────────────────────────────────────────────

void MiniWindowView::DidStartLoading() {
  UpdateNavButtons();
}

void MiniWindowView::DidStopLoading() {
  UpdateNavButtons();
  UpdateAddress();
  UpdateWindowTitle();
}

void MiniWindowView::TitleWasSet(content::NavigationEntry* entry) {
  UpdateWindowTitle();
}

BEGIN_METADATA(MiniWindowView)
END_METADATA

}  // namespace avora
