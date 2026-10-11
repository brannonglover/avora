// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/avora/avora_sidebar_view.h"

#include <algorithm>

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/avora/avora_defaults.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "components/vector_icons/vector_icons.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/image_loader.h"
#include "extensions/browser/ui_util.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_handlers/icons_handler.h"
#include "third_party/skia/include/core/SkColor.h"
#include "base/memory/raw_ref.h"
#include "base/numerics/safe_conversions.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/animation/slide_animation.h"
#include "skia/ext/image_operations.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/view_class_properties.h"
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
// The toggle renders a 20px glyph beside ~12px traffic-light circles.  With
// their centres matched the larger glyph overhangs the circles at the top and
// reads as sitting high, so it is nudged down to line the two up optically.
// This is the shift in pixels.
constexpr int kToggleOpticalNudge = 4;
constexpr int kNavIconSize = 17;
constexpr int kNavButtonGap = 4;
// Hover and press feedback for the nav-row icons: a white wash over the dark
// sidebar, kept faint on purpose, plus a small dip in the glyph while the
// button is held.  The glyph sits centred in its button so the wash is square
// around it, and the nav row takes the inset back out of its own padding so
// the glyphs stay where they were before the wash existed.
constexpr int kNavButtonCornerRadius = 8;
constexpr SkColor kNavButtonWash = SK_ColorWHITE;
constexpr int kNavButtonHoverAlpha = 0x14;
constexpr int kNavButtonPressAlpha = 0x26;
constexpr float kNavButtonPressShrink = 0.12f;
constexpr base::TimeDelta kNavHoverDuration = base::Milliseconds(110);
constexpr base::TimeDelta kNavPressInDuration = base::Milliseconds(60);
constexpr base::TimeDelta kNavPressOutDuration = base::Milliseconds(200);
// How far a centred glyph sits inside its button on every side, and how far
// the glyphs sit below the top of the nav row.  The row aligns its buttons to
// its leading edge, so these two land the glyphs exactly.
constexpr int kNavGlyphInset = (kNavButtonSize - kNavIconSize) / 2;
constexpr int kNavGlyphTop = 11;
constexpr int kSidebarHPadding = 10;
#if BUILDFLAG(IS_MAC)
// Width of the macOS close/minimise/zoom cluster measured from the window's
// leading edge, plus breathing room.  The sidebar's nav row starts at the
// window edge, so the toggle button has to clear this to sit beside them.
constexpr int kCaptionButtonsWidth = 82;
#else
constexpr int kCaptionButtonsWidth = 0;
#endif
constexpr int kAddressChipMinHeight = 36;
// How long a nav button has to be held before it opens its history menu
// instead of navigating.  Short enough that holding feels like a shortcut
// rather than a wait, long enough that an ordinary click never trips it.
constexpr base::TimeDelta kNavLongPressDelay = base::Milliseconds(400);

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

// ── Nav history menu ────────────────────────────────────────────────────────
// Wider than the manage bubble: these rows carry page titles, which need the
// room far more than a fixed list of commands does.
constexpr int kNavHistoryMenuWidth = 300;
constexpr int kNavHistoryRowHeight = 32;

// History rows draw whatever favicon they can get at one size: pages hand out
// anything from 16px to 256px, and a row of mismatched icons reads as broken.
// An empty image means none has arrived, and the row falls back to a globe.
ui::ImageModel NavHistoryIcon(const gfx::Image& favicon) {
  if (favicon.IsEmpty()) {
    return ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon,
                                          kBubbleRowIconColor, kBubbleIconSize);
  }
  gfx::ImageSkia icon = favicon.AsImageSkia();
  if (icon.width() != kBubbleIconSize || icon.height() != kBubbleIconSize) {
    icon = gfx::ImageSkiaOperations::CreateResizedImage(
        icon, skia::ImageOperations::RESIZE_BEST,
        gfx::Size(kBubbleIconSize, kBubbleIconSize));
  }
  return ui::ImageModel::FromImageSkia(icon);
}

std::unique_ptr<views::View> MakeSectionPlaceholder(int height) {
  auto section = std::make_unique<views::View>();
  section->SetPreferredSize(gfx::Size(0, height));
  return section;
}

// Nav-row icon button.  Avora draws the glyph and the button's own hover and
// press feedback rather than leaning on Chromium's image button and ink drop.
// The glyph is painted straight from its vector so it stays crisp while the
// press animation scales it.
class AvoraNavButton : public views::Button {
  METADATA_HEADER(AvoraNavButton, views::Button)

 public:
  AvoraNavButton(const gfx::VectorIcon& icon, PressedCallback callback)
      : views::Button(std::move(callback)), icon_(icon) {
    SetAnimateOnStateChange(true);
    SetAnimationDuration(kNavHoverDuration);
  }
  AvoraNavButton(const AvoraNavButton&) = delete;
  AvoraNavButton& operator=(const AvoraNavButton&) = delete;
  ~AvoraNavButton() override = default;

  // Gives the button a second gesture: held down (or right-clicked) it runs
  // |callback| instead of its press callback.  Used for the history menus.
  void SetLongPressCallback(base::RepeatingClosure callback) {
    long_press_cb_ = std::move(callback);
  }

  // views::Button:
  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (long_press_cb_ && event.IsRightMouseButton() &&
        HitTestPoint(event.location())) {
      long_press_cb_.Run();
      return true;
    }
    suppress_click_ = false;
    const bool handled = views::Button::OnMousePressed(event);
    if (long_press_cb_ && event.IsOnlyLeftMouseButton() &&
        HitTestPoint(event.location())) {
      long_press_timer_.Start(
          FROM_HERE, kNavLongPressDelay,
          base::BindOnce(&AvoraNavButton::OnLongPress,
                         base::Unretained(this)));
    }
    return handled;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    long_press_timer_.Stop();
    if (suppress_click_) {
      // The hold already opened the menu; releasing out of it must not also
      // navigate.  Drop the pressed look by hand, since the base class is the
      // thing being skipped.
      suppress_click_ = false;
      SetState(HitTestPoint(event.location()) ? STATE_HOVERED : STATE_NORMAL);
      return;
    }
    views::Button::OnMouseReleased(event);
  }

  void OnMouseCaptureLost() override {
    long_press_timer_.Stop();
    views::Button::OnMouseCaptureLost();
  }

  void OnGestureEvent(ui::GestureEvent* event) override {
    if (long_press_cb_ && event->type() == ui::EventType::kGestureLongPress) {
      suppress_click_ = true;
      event->SetHandled();
      long_press_cb_.Run();
      return;
    }
    views::Button::OnGestureEvent(event);
  }

 protected:
  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const float press = static_cast<float>(press_animation_.GetCurrentValue());
    // Button drops its hover animation to zero on the way into the pressed
    // state, so while the button is held the press value stands in for it.
    const float hover = std::max(
        static_cast<float>(hover_animation().GetCurrentValue()), press);
    const float wash_alpha =
        kNavButtonHoverAlpha * hover +
        (kNavButtonPressAlpha - kNavButtonHoverAlpha) * press;
    if (wash_alpha >= 1.0f) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setStyle(cc::PaintFlags::kFill_Style);
      flags.setColor(
          SkColorSetA(kNavButtonWash, base::ClampRound<U8CPU>(wash_alpha)));
      canvas->DrawRoundRect(GetLocalBounds(), kNavButtonCornerRadius, flags);
    }

    // The wash holds still; only the glyph dips, scaled about its own centre.
    gfx::ScopedCanvas scoped(canvas);
    if (press > 0.0f) {
      const float center = kNavGlyphInset + kNavIconSize / 2.0f;
      const float scale = 1.0f - kNavButtonPressShrink * press;
      canvas->sk_canvas()->translate(center, center);
      canvas->Scale(scale, scale);
      canvas->sk_canvas()->translate(-center, -center);
    }
    canvas->Translate(gfx::Vector2d(kNavGlyphInset, kNavGlyphInset));
    gfx::PaintVectorIcon(canvas, *icon_, kNavIconSize, kAvoraIconColor);
  }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    if (GetState() == STATE_PRESSED) {
      press_animation_.SetSlideDuration(kNavPressInDuration);
      press_animation_.Show();
    } else if (old_state == STATE_PRESSED) {
      press_animation_.SetSlideDuration(kNavPressOutDuration);
      press_animation_.Hide();
    }
  }

  void NotifyClick(const ui::Event& event) override {
    if (suppress_click_) {
      suppress_click_ = false;
      return;
    }
    // A fast click releases before the dip has travelled far, and keyboard
    // activation never passes through the pressed state at all.  Drop to the
    // bottom of the dip and spring back from there so every activation reads
    // the same.
    press_animation_.SetSlideDuration(kNavPressOutDuration);
    press_animation_.Reset(1);
    press_animation_.Hide();
    // The callback may tear this view down, so nothing may touch |this| after.
    views::Button::NotifyClick(event);
  }

 private:
  void OnLongPress() {
    // The click that would fire on release belongs to the hold, not to a
    // navigation.
    suppress_click_ = true;
    long_press_cb_.Run();
  }

  const raw_ref<const gfx::VectorIcon> icon_;
  gfx::SlideAnimation press_animation_{this};
  base::RepeatingClosure long_press_cb_;
  base::OneShotTimer long_press_timer_;
  bool suppress_click_ = false;
};

BEGIN_METADATA(AvoraNavButton)
END_METADATA

std::unique_ptr<views::Button> MakeNavButton(
    const gfx::VectorIcon& icon,
    base::RepeatingClosure callback,
    const std::u16string& tooltip,
    base::RepeatingClosure long_press_callback = base::RepeatingClosure()) {
  auto button = std::make_unique<AvoraNavButton>(icon, std::move(callback));
  button->SetPreferredSize(gfx::Size(kNavButtonSize, kNavButtonSize));
  button->SetTooltipText(tooltip);
  if (long_press_callback) {
    button->SetLongPressCallback(std::move(long_press_callback));
  }
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
      : ManageBubbleRow(ui::ImageModel::FromVectorIcon(icon,
                                                       kBubbleRowIconColor,
                                                       kBubbleIconSize),
                        text,
                        kBubbleWidth,
                        kBubbleRowHeight,
                        std::move(callback)) {}

  // Rows whose icon is not a glyph — a page favicon, say — and whose text can
  // run long enough to need a width of its own.
  ManageBubbleRow(const ui::ImageModel& image,
                  const std::u16string& text,
                  int width,
                  int height,
                  views::Button::PressedCallback callback)
      : views::LabelButton(std::move(callback), text) {
    SetImageModel(views::Button::STATE_NORMAL, image);
    SetTextColor(views::Button::STATE_NORMAL, kBubbleRowText);
    SetTextColor(views::Button::STATE_HOVERED, kBubbleRowText);
    SetTextColor(views::Button::STATE_PRESSED, kBubbleRowText);
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 12)));
    SetPreferredSize(gfx::Size(width, height));
    SetFocusBehavior(FocusBehavior::ALWAYS);
    label()->SetElideBehavior(gfx::ELIDE_TAIL);
  }

  ~ManageBubbleRow() override = default;

  base::WeakPtr<ManageBubbleRow> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

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

 private:
  base::WeakPtrFactory<ManageBubbleRow> weak_factory_{this};
};

BEGIN_METADATA(ManageBubbleRow)
END_METADATA


}  // namespace

// --- AvoraSidebarView -------------------------------------------------------

AvoraSidebarView::AvoraSidebarView(Profile* profile,
                                   AvoraNavCallback toggle_sidebar_cb,
                                   AvoraNavCallback back_cb,
                                   AvoraNavCallback forward_cb,
                                   AvoraNavCallback reload_cb,
                                   AvoraNavHistoryProvider nav_history_provider,
                                   AvoraNavHistoryActivateCallback
                                       nav_history_activate_cb,
                                   AvoraExtensionClickCallback extension_click_cb,
                                   AvoraNavCallback extensions_cb,
                                   AvoraNavCallback webstore_cb,
                                   AvoraNavCallback settings_cb,
                                   AvoraAddressCommitCallback address_commit_cb)
    : profile_(profile),
      nav_history_provider_(std::move(nav_history_provider)),
      nav_history_activate_cb_(std::move(nav_history_activate_cb)),
      extension_click_cb_(std::move(extension_click_cb)),
      extensions_cb_(std::move(extensions_cb)),
      webstore_cb_(std::move(webstore_cb)),
      settings_cb_(std::move(settings_cb)),
      address_commit_cb_(std::move(address_commit_cb)) {
  avora::ApplyBrowserDefaults(profile_->GetPrefs());

  SetBackground(views::CreateSolidBackground(kAvoraSidebarBg));
  // Compositing layer, matching VerticalTabStripRegionView.  The page is a
  // native view that draws above anything without one, so without this the
  // nav row and address chip vanishes behind the page whenever the sidebar is peeked
  // out over it.  The background above is opaque, so the layer hides the page
  // beneath it completely.
  SetPaintToLayer();

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // --- Nav row: back/forward/reload right-aligned (inline with stoplight) ---
  nav_row_ = AddChildView(MakeSectionPlaceholder(kNavRowHeight));
  auto* nav_layout =
      nav_row_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          // The button boxes are what the hover wash fills, so the row pads
          // by the wash's own edges: the trailing one lines up with the
          // address chip below, and the leading one keeps the toggle's glyph
          // clear of the traffic lights.
          gfx::Insets::TLBR(kNavGlyphTop - kNavGlyphInset,
                            kCaptionButtonsWidth - kNavGlyphInset, 0,
                            kSidebarHPadding),
          kNavButtonGap));
  nav_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kStart);
  nav_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStart);

  // Sidebar toggle, seated just right of the traffic lights.  A flexible
  // spacer after it keeps the nav buttons pinned to the trailing edge.
  toggle_sidebar_button_ = nav_row_->AddChildView(
      MakeNavButton(kDockToLeftIcon, std::move(toggle_sidebar_cb),
                    u"Hide sidebar (\u2318S)"));
  toggle_sidebar_button_->SetProperty(
      views::kMarginsKey,
      gfx::Insets::TLBR(kToggleOpticalNudge, 0, 0, 0));
  auto* const nav_spacer = nav_row_->AddChildView(
      std::make_unique<views::View>());
  nav_layout->SetFlexForView(nav_spacer, 1);

  // Held down (or right-clicked), these open the tab's session history in that
  // direction rather than stepping one entry.
  back_button_ = nav_row_->AddChildView(MakeNavButton(
      vector_icons::kArrowBackIcon, std::move(back_cb),
      u"Back (hold for history)",
      base::BindRepeating(&AvoraSidebarView::ShowNavHistoryMenu,
                          base::Unretained(this),
                          avora::NavHistoryDirection::kBack)));
  forward_button_ = nav_row_->AddChildView(MakeNavButton(
      vector_icons::kArrowForwardIcon, std::move(forward_cb),
      u"Forward (hold for history)",
      base::BindRepeating(&AvoraSidebarView::ShowNavHistoryMenu,
                          base::Unretained(this),
                          avora::NavHistoryDirection::kForward)));
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

void AvoraSidebarView::ShowNavHistoryMenu(
    avora::NavHistoryDirection direction) {
  if (!nav_history_provider_) {
    return;
  }
  std::vector<avora::NavHistoryEntry> entries =
      nav_history_provider_.Run(direction);
  if (entries.empty()) {
    // Nothing behind (or ahead of) this tab: the hold has nothing to show, and
    // an empty popup would read as a glitch.
    return;
  }

  views::View* const anchor = direction == avora::NavHistoryDirection::kBack
                                  ? static_cast<views::View*>(back_button_)
                                  : static_cast<views::View*>(forward_button_);
  if (!anchor) {
    return;
  }

  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor, views::BubbleBorder::TOP_RIGHT);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->set_margins(gfx::Insets::VH(6, 0));
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

  for (const avora::NavHistoryEntry& entry : entries) {
    std::u16string title =
        entry.title.empty() ? base::UTF8ToUTF16(entry.url.spec())
                            : entry.title;

    auto* row = wrapper->AddChildView(std::make_unique<ManageBubbleRow>(
        NavHistoryIcon(entry.favicon), title, kNavHistoryMenuWidth,
        kNavHistoryRowHeight,
        base::BindRepeating(
            [](views::BubbleDialogDelegate* d,
               AvoraNavHistoryActivateCallback cb, int index) {
              if (d->GetWidget()) {
                d->GetWidget()->Close();
              }
              if (cb) {
                cb.Run(index);
              }
            },
            base::Unretained(delegate_raw), nav_history_activate_cb_,
            entry.index)));
    // Titles elide, so the full address lives in the tooltip.
    row->SetTooltipText(base::UTF8ToUTF16(entry.url.spec()));
    if (entry.favicon.IsEmpty()) {
      LoadNavHistoryFavicon(row->GetWeakPtr(), entry.url);
    }
  }

  delegate->SetContentsView(std::move(wrapper));

  auto* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(delegate),
      views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  widget->Show();
}

void AvoraSidebarView::LoadNavHistoryFavicon(
    base::WeakPtr<views::LabelButton> row,
    const GURL& url) {
  if (!profile_ || !url.is_valid()) {
    return;
  }
  favicon::FaviconService* favicon_service =
      FaviconServiceFactory::GetForProfile(profile_,
                                           ServiceAccessType::EXPLICIT_ACCESS);
  if (!favicon_service) {
    return;
  }

  favicon_service->GetFaviconImageForPageURL(
      url,
      base::BindOnce(
          [](base::WeakPtr<views::LabelButton> row,
             const favicon_base::FaviconImageResult& result) {
            // The menu has usually closed by now; only a still-open one has a
            // row left to fill.
            if (!row || result.image.IsEmpty()) {
              return;
            }
            row->SetImageModel(views::Button::STATE_NORMAL,
                               NavHistoryIcon(result.image));
          },
          std::move(row)),
      &favicon_task_tracker_);
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

