// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_link_preview_view.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"

namespace avora {

namespace {

constexpr SkColor kBackdropColor = SkColorSetARGB(0x99, 0x10, 0x10, 0x18);
constexpr SkColor kCardBg = SkColorSetARGB(0xFF, 0x1E, 0x22, 0x2A);
constexpr SkColor kCardBorder = SkColorSetARGB(0x30, 0xFF, 0xFF, 0xFF);
constexpr SkColor kButtonBg = SkColorSetARGB(0xCC, 0x28, 0x2C, 0x34);
constexpr SkColor kButtonHoverBg = SkColorSetARGB(0xFF, 0x38, 0x3C, 0x46);
constexpr SkColor kButtonIconColor = SkColorSetARGB(0xDD, 0xED, 0xF2, 0xF5);

// Total width reserved for the button column to the right of the card.
constexpr int kButtonColumnWidth = 56;  // kButtonSize + gap

class RoundedBackground : public views::Background {
 public:
  RoundedBackground(SkColor fill, SkColor border, int radius)
      : fill_(fill), border_(border), radius_(radius) {}

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    const gfx::Rect bounds = view->GetLocalBounds();
    const float r = static_cast<float>(radius_);

    cc::PaintFlags fill_flags;
    fill_flags.setAntiAlias(true);
    fill_flags.setColor(fill_);
    fill_flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRoundRect(bounds, r, fill_flags);

    if (SkColorGetA(border_) > 0) {
      cc::PaintFlags border_flags;
      border_flags.setAntiAlias(true);
      border_flags.setColor(border_);
      border_flags.setStyle(cc::PaintFlags::kStroke_Style);
      border_flags.setStrokeWidth(1.0f);
      gfx::RectF br(bounds);
      br.Inset(0.5f);
      canvas->DrawRoundRect(br, r, border_flags);
    }
  }

 private:
  SkColor fill_;
  SkColor border_;
  int radius_;
};

class HoverImageButton : public views::ImageButton {
  METADATA_HEADER(HoverImageButton, views::ImageButton)

 public:
  HoverImageButton() = default;
  ~HoverImageButton() override = default;

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const gfx::Rect bounds = GetLocalBounds();
    const float r = bounds.width() / 2.0f;
    SkColor bg = kButtonBg;
    if (GetState() == views::Button::STATE_HOVERED ||
        GetState() == views::Button::STATE_PRESSED) {
      bg = kButtonHoverBg;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(bg);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRoundRect(bounds, r, flags);
  }
};

BEGIN_METADATA(HoverImageButton)
END_METADATA

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// AvoraLinkPreviewView
// ═══════════════════════════════════════════════════════════════════════════

AvoraLinkPreviewView::AvoraLinkPreviewView(
    BrowserWindowInterface* browser,
    LinkPreviewOpenCallback open_cb,
    LinkPreviewDismissCallback dismiss_cb)
    : browser_(browser),
      open_cb_(std::move(open_cb)),
      dismiss_cb_(std::move(dismiss_cb)) {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);

  auto backdrop = std::make_unique<views::View>();
  backdrop->SetPaintToLayer();
  backdrop->layer()->SetFillsBoundsOpaquely(false);
  backdrop->SetBackground(views::CreateSolidBackground(kBackdropColor));
  backdrop->SetCanProcessEventsWithinSubtree(false);
  backdrop_ = AddChildView(std::move(backdrop));

  SetFocusBehavior(FocusBehavior::ALWAYS);
  SetAccessibleRole(ax::mojom::Role::kDialog);
  SetAccessibleName(u"Link preview overlay");
  SetVisible(false);

  BuildUI();
}

AvoraLinkPreviewView::~AvoraLinkPreviewView() {
  if (owned_contents_) {
    owned_contents_->SetDelegate(nullptr);
  }
}

// ── Public API ──────────────────────────────────────────────────────────────

void AvoraLinkPreviewView::Show(const GURL& url) {
  previewed_url_ = url;

  Profile* profile = browser_->GetProfile();
  auto contents = content::WebContents::Create(
      content::WebContents::CreateParams(profile));
  contents->SetDelegate(this);

  owned_contents_ = std::move(contents);

  // Make visible and lay out BEFORE attaching the WebContents so the WebView
  // already has its final size — a zero-sized viewport stalls the renderer.
  SetVisible(true);
  LayoutCard();
  if (card_) {
    card_->DeprecatedLayoutImmediately();
  }

  if (web_view_) {
    web_view_->SetWebContents(owned_contents_.get());
  }

  // Start navigation after the WebContents is hosted in a sized view.
  owned_contents_->GetController().LoadURL(
      url, content::Referrer(), ui::PAGE_TRANSITION_LINK, std::string());

  RequestFocus();
}

void AvoraLinkPreviewView::ShowWithContents(
    std::unique_ptr<content::WebContents> contents,
    const GURL& url) {
  previewed_url_ = url;

  contents->SetDelegate(this);
  owned_contents_ = std::move(contents);

  // Make visible and lay out BEFORE attaching the WebContents so the WebView
  // already has its final size — a zero-sized viewport stalls the renderer.
  SetVisible(true);
  LayoutCard();
  if (card_) {
    card_->DeprecatedLayoutImmediately();
  }

  if (web_view_) {
    web_view_->SetWebContents(owned_contents_.get());
  }

  RequestFocus();
}

void AvoraLinkPreviewView::Hide() {
  SetVisible(false);
  if (web_view_) {
    web_view_->SetWebContents(nullptr);
  }
  if (owned_contents_) {
    owned_contents_->SetDelegate(nullptr);
    owned_contents_.reset();
  }
  previewed_url_ = GURL();
}

bool AvoraLinkPreviewView::IsShowing() const {
  return GetVisible();
}

// ── View overrides ──────────────────────────────────────────────────────────

bool AvoraLinkPreviewView::OnMousePressed(const ui::MouseEvent& event) {
  // Don't dismiss if the click landed on a button.
  if (close_button_) {
    gfx::Point pt = event.location();
    views::View::ConvertPointToTarget(this, close_button_, &pt);
    if (close_button_->HitTestPoint(pt)) {
      return false;
    }
  }
  if (open_tab_button_) {
    gfx::Point pt = event.location();
    views::View::ConvertPointToTarget(this, open_tab_button_, &pt);
    if (open_tab_button_->HitTestPoint(pt)) {
      return false;
    }
  }

  if (card_) {
    gfx::Point point_in_card = event.location();
    views::View::ConvertPointToTarget(this, card_, &point_in_card);
    if (!card_->HitTestPoint(point_in_card)) {
      Hide();
      if (dismiss_cb_) {
        dismiss_cb_.Run();
      }
      return true;
    }
  }
  return true;
}

bool AvoraLinkPreviewView::OnKeyPressed(const ui::KeyEvent& event) {
  if (event.key_code() == ui::VKEY_ESCAPE) {
    Hide();
    if (dismiss_cb_) {
      dismiss_cb_.Run();
    }
    return true;
  }
  return false;
}

void AvoraLinkPreviewView::OnBoundsChanged(
    const gfx::Rect& previous_bounds) {
  LayoutCard();
}

// ── WebContentsDelegate ─────────────────────────────────────────────────────

content::WebContents* AvoraLinkPreviewView::AddNewContents(
    content::WebContents* source,
    std::unique_ptr<content::WebContents> new_contents,
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features,
    bool user_gesture,
    bool* was_blocked) {
  if (open_cb_ && target_url.is_valid()) {
    open_cb_.Run(target_url);
  }
  return nullptr;
}

// ── Private helpers ─────────────────────────────────────────────────────────

void AvoraLinkPreviewView::BuildUI() {
  auto card = std::make_unique<views::View>();
  card->SetPaintToLayer();
  card->layer()->SetFillsBoundsOpaquely(false);
  card->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(kCardCornerRadius));

  card->SetBackground(std::make_unique<RoundedBackground>(
      kCardBg, kCardBorder, kCardCornerRadius));

  card->SetLayoutManager(std::make_unique<views::FillLayout>());

  Profile* profile = browser_->GetProfile();
  auto wv = std::make_unique<views::WebView>(profile);
  wv->SetPaintToLayer();
  wv->layer()->SetFillsBoundsOpaquely(false);
  wv->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(kCardCornerRadius));
  web_view_ = card->AddChildView(std::move(wv));

  card_ = AddChildView(std::move(card));

  // Buttons live to the right of the card, completely outside the WebView
  // bounds so the native platform window cannot occlude them.  Each button
  // needs its own compositing layer to paint above the backdrop layer which
  // covers the full overlay area.
  auto close_btn = std::make_unique<HoverImageButton>();
  close_btn->SetPaintToLayer();
  close_btn->layer()->SetFillsBoundsOpaquely(false);
  close_btn->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(
          vector_icons::kCloseIcon, kButtonIconColor, kButtonIconSize));
  close_btn->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  close_btn->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  close_btn->SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
  close_btn->SetAccessibleName(u"Close preview");
  close_btn->SetCallback(base::BindRepeating(
      &AvoraLinkPreviewView::OnCloseClicked, weak_factory_.GetWeakPtr()));
  close_button_ = AddChildView(std::move(close_btn));

  auto open_btn = std::make_unique<HoverImageButton>();
  open_btn->SetPaintToLayer();
  open_btn->layer()->SetFillsBoundsOpaquely(false);
  open_btn->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(
          kOpenInFullIcon, kButtonIconColor, kButtonIconSize));
  open_btn->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  open_btn->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  open_btn->SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
  open_btn->SetAccessibleName(u"Open in new tab");
  open_btn->SetCallback(base::BindRepeating(
      &AvoraLinkPreviewView::OnOpenInNewTabClicked,
      weak_factory_.GetWeakPtr()));
  open_tab_button_ = AddChildView(std::move(open_btn));
}

void AvoraLinkPreviewView::LayoutCard() {
  if (!card_) {
    return;
  }

  const gfx::Size host = size();
  if (host.IsEmpty()) {
    return;
  }

  if (backdrop_) {
    backdrop_->SetBoundsRect(gfx::Rect(host));
    constexpr float kWindowRadius = 10.0f;
    backdrop_->layer()->SetRoundedCornerRadius(
        gfx::RoundedCornersF(kWindowRadius));
    backdrop_->layer()->SetIsFastRoundedCorner(true);
  }

  // Card fills ~80 % of the window width (minus button column + padding).
  int desired_card_width = static_cast<int>(host.width() * 0.80);
  int total_width = desired_card_width + kButtonColumnWidth;
  if (total_width > host.width() - 32) {
    total_width = host.width() - 32;
  }
  int card_width = total_width - kButtonColumnWidth;
  card_width = std::max(card_width, 400);

  // Center the card visually; buttons hang off its right edge.
  int card_x = (host.width() - card_width) / 2;
  int card_y = kCardVerticalMargin;
  int card_height = host.height() - 2 * kCardVerticalMargin;
  card_height = std::max(card_height, 200);

  card_->SetBoundsRect(gfx::Rect(card_x, card_y, card_width, card_height));

  // Buttons sit to the right of the card, vertically stacked near the top.
  int btn_x = card_x + card_width + (kButtonColumnWidth - kButtonSize) / 2;
  int btn_y = card_y + kButtonMargin;

  if (close_button_) {
    close_button_->SetBoundsRect(
        gfx::Rect(btn_x, btn_y, kButtonSize, kButtonSize));
  }
  if (open_tab_button_) {
    open_tab_button_->SetBoundsRect(
        gfx::Rect(btn_x, btn_y + kButtonSize + kButtonSpacing,
                   kButtonSize, kButtonSize));
  }
}

void AvoraLinkPreviewView::OnCloseClicked() {
  Hide();
  if (dismiss_cb_) {
    dismiss_cb_.Run();
  }
}

void AvoraLinkPreviewView::OnOpenInNewTabClicked() {
  GURL url = previewed_url_;
  Hide();
  if (open_cb_ && url.is_valid()) {
    open_cb_.Run(url);
  }
  if (dismiss_cb_) {
    dismiss_cb_.Run();
  }
}

BEGIN_METADATA(AvoraLinkPreviewView)
END_METADATA

}  // namespace avora
