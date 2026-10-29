// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_link_preview_view.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/avora/avora_link_preview_tab_state.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/gfx/geometry/skia_conversions.h"
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
#include "ui/views/view_targeter.h"

namespace avora {

namespace {

constexpr SkColor kBackdropColor = SkColorSetARGB(0x99, 0x10, 0x10, 0x18);
constexpr SkColor kButtonBg = SkColorSetARGB(0xCC, 0x28, 0x2C, 0x34);
constexpr SkColor kButtonHoverBg = SkColorSetARGB(0xFF, 0x38, 0x3C, 0x46);
constexpr SkColor kButtonIconColor = SkColorSetARGB(0xDD, 0xED, 0xF2, 0xF5);

// Total width reserved for the button column to the right of the card.
constexpr int kButtonColumnWidth = 56;  // kButtonSize + gap

// Dims the whole overlay except for a rounded hole where the card sits.  The
// page is drawn by a contents container beneath this view, so the backdrop
// must leave that rect alone rather than tinting the preview.
class BackdropBackground : public views::Background {
 public:
  BackdropBackground(SkColor fill, int radius) : fill_(fill), radius_(radius) {}

  void set_hole(const gfx::Rect& hole) { hole_ = hole; }

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(fill_);
    flags.setStyle(cc::PaintFlags::kFill_Style);

    if (hole_.IsEmpty()) {
      canvas->DrawRect(view->GetLocalBounds(), flags);
      return;
    }

    // Clip the card's rounded rect out of the dim rather than painting around
    // it, so the page beneath shows through at full contrast with a clean
    // anti-aliased edge.
    const float r = static_cast<float>(radius_);
    canvas->Save();
    canvas->sk_canvas()->clipRRect(
        SkRRect::MakeRectXY(gfx::RectToSkRect(hole_), r, r),
        SkClipOp::kDifference, /*do_anti_alias=*/true);
    canvas->DrawRect(view->GetLocalBounds(), flags);
    canvas->Restore();
  }

 private:
  SkColor fill_;
  int radius_;
  gfx::Rect hole_;
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
    LinkPreviewPromoteCallback promote_cb,
    LinkPreviewDismissCallback dismiss_cb,
    LinkPreviewReturnCallback return_cb)
    : browser_(browser),
      promote_cb_(std::move(promote_cb)),
      dismiss_cb_(std::move(dismiss_cb)),
      return_cb_(std::move(return_cb)) {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);

  // Needed for DoesIntersectRect() below, which is what lets events over the
  // card reach the contents container hosting the page.
  SetEventTargeter(std::make_unique<views::ViewTargeter>(this));

  auto backdrop = std::make_unique<views::View>();
  backdrop->SetPaintToLayer();
  backdrop->layer()->SetFillsBoundsOpaquely(false);
  backdrop->SetBackground(
      std::make_unique<BackdropBackground>(kBackdropColor, kCardCornerRadius));
  backdrop->SetCanProcessEventsWithinSubtree(false);
  backdrop_ = AddChildView(std::move(backdrop));

  SetFocusBehavior(FocusBehavior::ALWAYS);
  SetAccessibleRole(ax::mojom::Role::kDialog);
  SetAccessibleName(u"Link preview overlay");
  SetVisible(false);

  BuildUI();
}

AvoraLinkPreviewView::~AvoraLinkPreviewView() = default;

// ── Public API ──────────────────────────────────────────────────────────────

void AvoraLinkPreviewView::AttachPreview(content::WebContents* host_tab,
                                        content::WebContents* preview,
                                        const GURL& url) {
  if (!preview) {
    return;
  }
  if (attached_preview_ == preview) {
    return;
  }
  DetachPreview();

  host_tab_ = host_tab ? host_tab->GetWeakPtr()
                       : base::WeakPtr<content::WebContents>();
  attached_preview_ = preview;
  previewed_url_ = url;
  // No SetDelegate(): the preview is a tab, so the browser is already its
  // delegate and must stay that way.
  content::WebContentsObserver::Observe(preview);

  // Make visible and lay out BEFORE attaching the WebContents so the WebView
  // already has its final size — a zero-sized viewport stalls the renderer.
  SetVisible(true);
  LayoutCard();
  if (card_) {
    card_->DeprecatedLayoutImmediately();
  }

  // The page is mounted by the embedder into the contents container at
  // card_bounds(); nothing is attached here.  Visibility is still set here,
  // because the preview is not yet the tab strip's foreground tab at this
  // point -- the embedder activates it immediately after.
  preview->WasShown();

  if (card_bounds_changed_) {
    card_bounds_changed_.Run();
  }

  // The sign-in may already have landed: while the preview was parked behind
  // another tab, or before the card was there to watch it commit.
  MaybeReturnToHost();
}

gfx::Rect AvoraLinkPreviewView::card_bounds() const {
  return card_ ? card_->bounds() : gfx::Rect();
}

// static
gfx::RoundedCornersF AvoraLinkPreviewView::card_radii() {
  return gfx::RoundedCornersF(kCardCornerRadius);
}

bool AvoraLinkPreviewView::DoesIntersectRect(const views::View* target,
                                             const gfx::Rect& rect) const {
  // Let events over the card fall through to the contents container below,
  // which is where the page actually lives.  The buttons sit outside the
  // card's rect, so they keep receiving events normally.
  if (target == this && card_ && card_->bounds().Contains(rect)) {
    return false;
  }
  return views::ViewTargeterDelegate::DoesIntersectRect(target, rect);
}

void AvoraLinkPreviewView::DetachPreview() {
  SetVisible(false);
  // The page was never attached here, so there is nothing to release: the
  // embedder unmounts it from the contents container.
  if (attached_preview_ && !attached_preview_->IsBeingDestroyed()) {
    // Back to being an ordinary background tab: alive, but not painting.
    attached_preview_->WasHidden();
  }
  content::WebContentsObserver::Observe(nullptr);
  attached_preview_ = nullptr;
  host_tab_.reset();
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

// ── WebContentsObserver ─────────────────────────────────────────────────────

void AvoraLinkPreviewView::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  // A sign-in comes back as a document load; a same-document commit is the
  // page rearranging itself, not a return.
  if (!navigation_handle->IsInPrimaryMainFrame() ||
      !navigation_handle->HasCommitted() ||
      navigation_handle->IsSameDocument() ||
      navigation_handle->IsErrorPage()) {
    return;
  }
  MaybeReturnToHost();
}

void AvoraLinkPreviewView::MaybeReturnToHost() {
  if (return_cb_ && attached_preview_ &&
      IsLinkPreviewReturnToHost(host_tab_.get(), attached_preview_)) {
    return_cb_.Run(attached_preview_->GetLastCommittedURL());
  }
}

void AvoraLinkPreviewView::WebContentsDestroyed() {
  // The preview tab was closed underneath us.  Drop the pointer first so the
  // detach path does not touch a half-destroyed WebContents.
  attached_preview_ = nullptr;
  DetachPreview();
}

// ── Private helpers ─────────────────────────────────────────────────────────

void AvoraLinkPreviewView::BuildUI() {
  // Geometry anchor only.  The page is hosted by MultiContentsView's
  // secondary contents container, positioned at these bounds, which draws the
  // page and its rounded corners itself; anything painted here would cover
  // it.  Kept as a view so the buttons and the backdrop's hole have one
  // source of truth for the card's rect.
  auto card = std::make_unique<views::View>();
  card->SetCanProcessEventsWithinSubtree(false);
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
  open_btn->SetAccessibleName(u"Open in a tab");
  open_btn->SetCallback(base::BindRepeating(
      &AvoraLinkPreviewView::OnPromoteToTabClicked,
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

  const gfx::Rect previous_card = card_->bounds();

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

  // Punch the card out of the backdrop so the page beneath shows through
  // undimmed, and tell the embedder where to put the contents container.
  if (backdrop_) {
    static_cast<BackdropBackground*>(backdrop_->GetBackground())
        ->set_hole(card_->bounds());
    backdrop_->SchedulePaint();
  }
  if (card_->bounds() != previous_card && card_bounds_changed_) {
    card_bounds_changed_.Run();
  }
}

void AvoraLinkPreviewView::OnCloseClicked() {
  if (dismiss_cb_) {
    dismiss_cb_.Run();
  }
}

void AvoraLinkPreviewView::OnPromoteToTabClicked() {
  // Deliberately no dismiss_cb_ here: dismissing closes the preview tab, and
  // the whole point of expanding is that the page survives the gesture with
  // everything the user typed, scrolled or loaded still in place.  The
  // promotion path detaches this overlay itself as the preview becomes the
  // active tab.
  if (promote_cb_) {
    promote_cb_.Run();
  }
}

BEGIN_METADATA(AvoraLinkPreviewView)
END_METADATA

}  // namespace avora
