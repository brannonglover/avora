// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_import_offer_view.h"

#include <algorithm>

#include "base/command_line.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/avora/avora_prefs.h"
#include "chrome/browser/avora/import/avora_import_coordinator.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/avora/avora_import_dialog_view.h"
#include "chrome/browser/ui/views/avora/avora_lucide_icon.h"
#include "components/prefs/pref_service.h"
#include "ui/base/base_window.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/text_constants.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"

namespace avora {

namespace {

// ── Colour palette ──────────────────────────────────────────────────────────
// Shared with the quick-nav card and the toast: the same dark surface, the
// same #EDF2F5 text ramp, the same #6EA1F7 accent, so the first thing a new
// user sees already looks like the rest of Avora.
constexpr SkColor kCardBg = SkColorSetRGB(0x1E, 0x22, 0x2A);
constexpr SkColor kTitleColor = SkColorSetRGB(0xED, 0xF2, 0xF5);
constexpr SkColor kBodyColor = SkColorSetARGB(0xB8, 0xED, 0xF2, 0xF5);
constexpr SkColor kDetailColor = SkColorSetARGB(0x7A, 0xED, 0xF2, 0xF5);
constexpr SkColor kCountColor = SkColorSetARGB(0x99, 0xED, 0xF2, 0xF5);
constexpr SkColor kListBg = SkColorSetARGB(0x0D, 0xFF, 0xFF, 0xFF);
constexpr SkColor kListBorder = SkColorSetARGB(0x1A, 0xFF, 0xFF, 0xFF);
constexpr SkColor kAccent = SkColorSetRGB(0x6E, 0xA1, 0xF7);
// Text on the accent button: the sidebar's darkest tone, not black, so the
// button reads as part of the surface rather than punched out of it.
constexpr SkColor kAccentText = SkColorSetRGB(0x10, 0x14, 0x1A);
constexpr SkColor kGhostFill = SkColorSetARGB(0x0F, 0xFF, 0xFF, 0xFF);
constexpr SkColor kWhite = SK_ColorWHITE;

// ── Metrics ─────────────────────────────────────────────────────────────────
constexpr int kDialogWidth = 420;
constexpr int kPadding = 24;
constexpr int kHeaderIconSize = 28;
constexpr int kHeaderIconGlyph = 16;
constexpr int kHeaderIconRadius = 8;
constexpr int kHeaderGap = 12;
constexpr int kListRadius = 12;
constexpr int kListVInset = 5;
constexpr int kRowHeight = 34;
constexpr int kRowHInset = 12;
constexpr int kRowGap = 10;
constexpr int kBadgeSize = 20;
constexpr int kBadgeRadius = 6;
constexpr int kButtonHeight = 32;
constexpr int kButtonRadius = 8;
constexpr int kButtonHInset = 16;
constexpr int kButtonGap = 8;

// Hover and focus washes for the footer buttons.
constexpr float kAccentHoverLift = 0.14f;
constexpr int kGhostHoverAlpha = 0x1C;

// Lucide "bookmark", the header glyph.
constexpr char kBookmarkIconPath[] =
    "M19 21l-7-5-7 5V5a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2z";

gfx::FontList OfferFont(int size, gfx::Font::Weight weight) {
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

// A browser's identity colour, used at low alpha behind its initial.  Muted
// towards Avora's palette rather than lifted straight from each vendor's
// brand sheet, so nine rows of badges stay calm next to one another.
SkColor BrowserTint(const std::string& browser) {
  if (browser == "chrome") {
    return SkColorSetRGB(0xF2, 0xB4, 0x41);
  }
  if (browser == "edge") {
    return SkColorSetRGB(0x4F, 0xB3, 0xD9);
  }
  if (browser == "firefox") {
    return SkColorSetRGB(0xFF, 0x8A, 0x50);
  }
  if (browser == "safari") {
    return SkColorSetRGB(0x5A, 0xA9, 0xE6);
  }
  if (browser == "arc") {
    return SkColorSetRGB(0xE8, 0x83, 0xAE);
  }
  return kAccent;
}

// The letter in the badge.  Spelled out per browser because the display names
// do not all start with the memorable letter ("Microsoft Edge" reads as E).
std::u16string BrowserInitial(const std::string& browser,
                              const std::u16string& display_name) {
  if (browser == "chrome") {
    return u"C";
  }
  if (browser == "edge") {
    return u"E";
  }
  if (browser == "firefox") {
    return u"F";
  }
  if (browser == "safari") {
    return u"S";
  }
  if (browser == "arc") {
    return u"A";
  }
  return display_name.empty() ? u"?" : display_name.substr(0, 1);
}

// ── Rounded surface ─────────────────────────────────────────────────────────

class RoundedFill : public views::Background {
 public:
  RoundedFill(SkColor fill, SkColor border, int radius)
      : fill_(fill), border_(border), radius_(radius) {}

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    const gfx::Rect bounds = view->GetLocalBounds();
    FillRoundRect(canvas, bounds, fill_, radius_);

    if (SkColorGetA(border_) == 0) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1.0f);
    flags.setColor(border_);
    gfx::RectF rect(bounds);
    rect.Inset(0.5f);
    canvas->DrawRoundRect(rect, radius_, flags);
  }

 private:
  const SkColor fill_;
  const SkColor border_;
  const int radius_;
};

// ── Header glyph ────────────────────────────────────────────────────────────

class HeaderIcon : public views::View {
  METADATA_HEADER(HeaderIcon, views::View)

 public:
  HeaderIcon() {
    SetPreferredSize(gfx::Size(kHeaderIconSize, kHeaderIconSize));
    SetCanProcessEventsWithinSubtree(false);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    FillRoundRect(canvas, GetLocalBounds(), SkColorSetA(kAccent, 0x26),
                  kHeaderIconRadius);
    gfx::Rect glyph = GetLocalBounds();
    glyph.ClampToCenteredSize(gfx::Size(kHeaderIconGlyph, kHeaderIconGlyph));
    PaintLucidePathData(canvas, glyph, kBookmarkIconPath, kAccent);
  }
};

BEGIN_METADATA(HeaderIcon)
END_METADATA

// ── Source badge ────────────────────────────────────────────────────────────

class BrowserBadge : public views::View {
  METADATA_HEADER(BrowserBadge, views::View)

 public:
  BrowserBadge(SkColor tint, std::u16string initial)
      : tint_(tint), initial_(std::move(initial)) {
    SetPreferredSize(gfx::Size(kBadgeSize, kBadgeSize));
    SetCanProcessEventsWithinSubtree(false);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    FillRoundRect(canvas, GetLocalBounds(), SkColorSetA(tint_, 0x2E),
                  kBadgeRadius);
    canvas->DrawStringRectWithFlags(
        initial_, OfferFont(11, gfx::Font::Weight::SEMIBOLD), tint_,
        GetLocalBounds(), gfx::Canvas::TEXT_ALIGN_CENTER);
  }

 private:
  const SkColor tint_;
  const std::u16string initial_;
};

BEGIN_METADATA(BrowserBadge)
END_METADATA

// ── Source row ──────────────────────────────────────────────────────────────

// One detected browser: badge, name, the profiles found inside it, and the
// total bookmark count.  Rolling the profiles into their browser keeps the
// list to one line per browser -- a Firefox with four profiles no longer
// pushes the buttons off the bottom of the card.
class SourceRow : public views::View {
  METADATA_HEADER(SourceRow, views::View)

 public:
  SourceRow(const std::string& browser_id,
            const std::u16string& display_name,
            const std::u16string& profiles,
            int count) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(0, kRowHInset), kRowGap));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    AddChildView(std::make_unique<BrowserBadge>(
        BrowserTint(browser_id), BrowserInitial(browser_id, display_name)));

    auto* name = AddChildView(std::make_unique<views::Label>(
        display_name,
        views::Label::CustomFont{OfferFont(13, gfx::Font::Weight::MEDIUM)}));
    name->SetEnabledColor(kTitleColor);
    name->SetHorizontalAlignment(gfx::ALIGN_LEFT);

    auto* detail = AddChildView(std::make_unique<views::Label>(
        profiles,
        views::Label::CustomFont{OfferFont(12, gfx::Font::Weight::NORMAL)}));
    detail->SetEnabledColor(kDetailColor);
    detail->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    detail->SetElideBehavior(gfx::ELIDE_TAIL);
    // The profile names give up room first; the name and count never elide.
    layout->SetFlexForView(detail, 1);

    auto* count_label = AddChildView(std::make_unique<views::Label>(
        base::NumberToString16(count),
        views::Label::CustomFont{OfferFont(12, gfx::Font::Weight::MEDIUM)}));
    count_label->SetEnabledColor(kCountColor);
    count_label->SetHorizontalAlignment(gfx::ALIGN_RIGHT);

    SetPreferredSize(gfx::Size(kDialogWidth - 2 * kPadding, kRowHeight));
  }
};

BEGIN_METADATA(SourceRow)
END_METADATA

// ── Footer button ───────────────────────────────────────────────────────────

class PillButton : public views::Button {
  METADATA_HEADER(PillButton, views::Button)

 public:
  enum class Style { kAccent, kGhost };

  PillButton(const std::u16string& text, Style style, PressedCallback callback)
      : views::Button(std::move(callback)),
        text_(text),
        style_(style),
        font_(OfferFont(13, style == Style::kAccent
                                ? gfx::Font::Weight::SEMIBOLD
                                : gfx::Font::Weight::MEDIUM)) {
    SetAnimateOnStateChange(true);
    SetPreferredSize(gfx::Size(
        gfx::GetStringWidth(text_, font_) + 2 * kButtonHInset, kButtonHeight));
    SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
    GetViewAccessibility().SetName(text_);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const float hover =
        static_cast<float>(std::max(hover_animation().GetCurrentValue(),
                                    GetState() == STATE_PRESSED ? 1.0 : 0.0));
    const gfx::Rect bounds = GetLocalBounds();

    SkColor fill = SK_ColorTRANSPARENT;
    SkColor text_color = kBodyColor;
    if (style_ == Style::kAccent) {
      fill = color_utils::AlphaBlend(kWhite, kAccent,
                                     kAccentHoverLift * hover);
      text_color = kAccentText;
    } else {
      fill = SkColorSetA(kWhite, SkColorGetA(kGhostFill) +
                                     base::ClampRound<U8CPU>(
                                         kGhostHoverAlpha * hover));
      text_color = hover > 0.f ? kTitleColor : kBodyColor;
    }
    FillRoundRect(canvas, bounds, fill, kButtonRadius);

    // The ring is a keyboard affordance only.  A dialog that opens with a ring
    // already drawn around its primary button reads as a Chromium dialog, so
    // it appears the moment focus arrives by Tab and not before.
    if (HasFocus() && GetFocusManager() &&
        GetFocusManager()->focus_change_reason() ==
            views::FocusManager::FocusChangeReason::kFocusTraversal) {
      cc::PaintFlags ring;
      ring.setAntiAlias(true);
      ring.setStyle(cc::PaintFlags::kStroke_Style);
      ring.setStrokeWidth(2.0f);
      ring.setColor(SkColorSetA(kAccent, 0xCC));
      gfx::RectF rect(bounds);
      rect.Inset(1.0f);
      canvas->DrawRoundRect(rect, kButtonRadius - 1, ring);
    }

    canvas->DrawStringRectWithFlags(text_, font_, text_color, bounds,
                                    gfx::Canvas::TEXT_ALIGN_CENTER);
  }

  void OnFocus() override {
    views::Button::OnFocus();
    SchedulePaint();
  }

  void OnBlur() override {
    views::Button::OnBlur();
    SchedulePaint();
  }

 private:
  const std::u16string text_;
  const Style style_;
  const gfx::FontList font_;
};

BEGIN_METADATA(PillButton)
END_METADATA

}  // namespace

// static
bool AvoraImportOfferView::MaybeShow(BrowserWindowInterface* browser,
                                     WindowSpaceState* window_space_state) {
  if (!browser) return false;

  // Test-only escape hatch.  This dialog is a Widget that can outlive the
  // BrowserWindowInterface and WindowSpaceState it holds raw_ptrs to, which a
  // DCHECK build reports as a dangling pointer during window teardown and
  // which aborts every browser test in the process.  Suppressing the offer
  // lets test binaries run under DCHECK while that lifetime bug is fixed
  // separately -- it is a workaround for test runs, NOT a fix: the dangling
  // pointer is still there for any real user who sees this dialog.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          kSuppressImportOfferSwitch)) {
    return false;
  }

  Profile* profile = browser->GetProfile();
  if (!profile) return false;
  PrefService* prefs = profile->GetPrefs();
  if (!prefs) return false;

  if (prefs->GetBoolean(kImportOfferedPref)) return false;

  auto browsers = AvoraImportCoordinator::DetectAllBrowsers();

  // Only show if we found at least one importable profile.
  bool any_importable = false;
  for (const auto& b : browsers) {
    for (const auto& p : b.profiles) {
      if (p.bookmark_count > 0) {
        any_importable = true;
        break;
      }
    }
    if (any_importable) break;
  }

  // Do not consume the one-time offer when detection found nothing.
  // A transient detection failure should not permanently suppress the offer.
  if (!any_importable) return false;

  // Mark as offered now that we are about to present the dialog.
  prefs->SetBoolean(kImportOfferedPref, true);

  auto delegate = std::make_unique<views::DialogDelegate>();
  views::DialogDelegate* delegate_ptr = delegate.get();
  delegate_ptr->SetContentsView(std::make_unique<AvoraImportOfferView>(
      std::move(delegate), browser, window_space_state, std::move(browsers)));
  views::Widget* widget = views::DialogDelegate::CreateDialogWidget(
      delegate_ptr, browser->GetWindow()->GetNativeWindow(),
      gfx::NativeView());

  // Avora: the frame is Chromium's, but its fill is not.  Repainting it in the
  // card colour is what stops a strip of Chromium grey showing around the
  // contents; everything inside the frame is drawn by this view.
  if (auto* frame = views::AsViewClass<views::BubbleFrameView>(
          widget->non_client_view()->frame_view())) {
    frame->SetBackgroundColor(kCardBg);
  }
  widget->Show();
  return true;
}

AvoraImportOfferView::AvoraImportOfferView(
    std::unique_ptr<views::DialogDelegate> delegate,
    BrowserWindowInterface* browser,
    WindowSpaceState* window_space_state,
    std::vector<DetectedBrowser> browsers)
    : delegate_(std::move(delegate)),
      browser_(browser),
      window_space_state_(window_space_state),
      browsers_(std::move(browsers)) {
  // The title stays set for the window title and for screen readers, but the
  // frame does not draw it: Avora draws its own header below, and a
  // frame-drawn title would repeat it.
  delegate_->SetTitle(u"Bring your bookmarks to Avora");
  delegate_->SetShowTitle(false);
  delegate_->SetShowCloseButton(false);
  delegate_->SetModalType(ui::mojom::ModalType::kWindow);

  // Avora draws its own footer, so Chromium contributes no buttons.  The
  // accept and cancel paths still run through the delegate, which keeps Esc
  // cancelling the dialog.
  delegate_->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate_->set_margins(gfx::Insets());
  // With no frame-drawn title the frame still reserves its title margin above
  // the contents, which lands as a strip of Chromium grey crowding Avora's own
  // header.  Zeroing it hands the whole frame to this view.
  delegate_->set_title_margins(gfx::Insets());
  delegate_->set_fixed_width(kDialogWidth);

  // `delegate_` is owned by this view, so it can never outlive `this`.
  delegate_->SetAcceptCallback(base::BindOnce(
      &AvoraImportOfferView::OnAccept, base::Unretained(this)));

  SetBackground(std::make_unique<RoundedFill>(kCardBg, SK_ColorTRANSPARENT, 0));
  SetBorder(views::CreateEmptyBorder(gfx::Insets(kPadding)));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  const int content_width = kDialogWidth - 2 * kPadding;

  // ── Header: glyph, title, and what was actually found ────────────────────
  auto* header = AddChildView(std::make_unique<views::View>());
  auto* header_layout =
      header->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          kHeaderGap));
  header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStart);
  header->AddChildView(std::make_unique<HeaderIcon>());

  auto* header_text = header->AddChildView(std::make_unique<views::View>());
  header_text->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
  header_layout->SetFlexForView(header_text, 1);

  const int text_width = content_width - kHeaderIconSize - kHeaderGap;

  auto* title = header_text->AddChildView(std::make_unique<views::Label>(
      u"Bring your bookmarks to Avora",
      views::Label::CustomFont{OfferFont(17, gfx::Font::Weight::SEMIBOLD)}));
  title->SetEnabledColor(kTitleColor);
  title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title->SetMultiLine(true);
  title->SetMaximumWidth(text_width);

  auto* summary = header_text->AddChildView(std::make_unique<views::Label>(
      SummaryText(),
      views::Label::CustomFont{OfferFont(12, gfx::Font::Weight::NORMAL)}));
  summary->SetEnabledColor(kBodyColor);
  summary->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  summary->SetMultiLine(true);
  summary->SetMaximumWidth(text_width);

  AddChildView(std::make_unique<views::View>())
      ->SetPreferredSize(gfx::Size(content_width, 18));

  // ── The detected browsers ────────────────────────────────────────────────
  auto* list = AddChildView(std::make_unique<views::View>());
  list->SetBackground(
      std::make_unique<RoundedFill>(kListBg, kListBorder, kListRadius));
  list->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(kListVInset, 0), 0));

  for (const auto& browser_data : browsers_) {
    int count = 0;
    std::u16string profiles;
    for (const auto& p : browser_data.profiles) {
      if (p.bookmark_count <= 0) {
        continue;
      }
      count += p.bookmark_count;
      if (!profiles.empty()) {
        profiles += u" · ";
      }
      profiles += base::UTF8ToUTF16(p.display_name);
    }
    if (count == 0) {
      continue;
    }

    list->AddChildView(std::make_unique<SourceRow>(
        browser_data.browser,
        AvoraImportDialogView::BrowserDisplayName(browser_data.browser),
        profiles, count));
  }

  AddChildView(std::make_unique<views::View>())
      ->SetPreferredSize(gfx::Size(content_width, 14));

  // ── Where the bookmarks land ─────────────────────────────────────────────
  auto* explanation = AddChildView(std::make_unique<views::Label>(
      u"Your bookmarks stay organized in Imported. Drag the ones you use "
      u"most to Favorites.",
      views::Label::CustomFont{OfferFont(12, gfx::Font::Weight::NORMAL)}));
  explanation->SetEnabledColor(kDetailColor);
  explanation->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  explanation->SetMultiLine(true);
  explanation->SetMaximumWidth(content_width);

  AddChildView(std::make_unique<views::View>())
      ->SetPreferredSize(gfx::Size(content_width, 20));

  // ── Footer ───────────────────────────────────────────────────────────────
  auto* footer = AddChildView(std::make_unique<views::View>());
  auto* footer_layout =
      footer->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          kButtonGap));
  footer_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kEnd);

  footer->AddChildView(std::make_unique<PillButton>(
      u"Not now", PillButton::Style::kGhost,
      base::BindRepeating(&AvoraImportOfferView::OnDismissPressed,
                          base::Unretained(this))));

  footer->AddChildView(std::make_unique<PillButton>(
      u"Import bookmarks", PillButton::Style::kAccent,
      base::BindRepeating(&AvoraImportOfferView::OnImportPressed,
                          base::Unretained(this))));
}

AvoraImportOfferView::~AvoraImportOfferView() = default;

std::u16string AvoraImportOfferView::SummaryText() const {
  int bookmarks = 0;
  int sources = 0;
  for (const auto& browser_data : browsers_) {
    int count = 0;
    for (const auto& p : browser_data.profiles) {
      if (p.bookmark_count > 0) {
        count += p.bookmark_count;
      }
    }
    if (count > 0) {
      bookmarks += count;
      ++sources;
    }
  }

  return u"We found " + base::NumberToString16(bookmarks) +
         (bookmarks == 1 ? u" bookmark in " : u" bookmarks in ") +
         base::NumberToString16(sources) +
         (sources == 1 ? u" browser." : u" browsers.");
}

void AvoraImportOfferView::OnImportPressed() {
  delegate_->AcceptDialog();
}

void AvoraImportOfferView::OnDismissPressed() {
  delegate_->CancelDialog();
}

void AvoraImportOfferView::OnAccept() {
  // Open the full import dialog.
  AvoraImportDialogView::Show(browser_, window_space_state_);
}

BEGIN_METADATA(AvoraImportOfferView)
END_METADATA

}  // namespace avora
