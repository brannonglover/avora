// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_quick_nav_view.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/avora/avora_search_suggest.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/storage_partition.h"
#include "ui/gfx/paint_vector_icon.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "url/gurl.h"

namespace avora {

namespace {

// ── Colour palette ──────────────────────────────────────────────────────────
constexpr SkColor kBackdropColor = SkColorSetARGB(0x44, 0x10, 0x10, 0x18);

constexpr SkColor kCardBg = SkColorSetARGB(0xDD, 0x1E, 0x22, 0x2A);
constexpr SkColor kCardBorder = SkColorSetARGB(0x30, 0xFF, 0xFF, 0xFF);

constexpr SkColor kInputBg = SkColorSetRGB(0x14, 0x17, 0x1C);
constexpr SkColor kInputBorderFocused = SkColorSetARGB(0x66, 0x6E, 0xA1, 0xF7);
constexpr SkColor kInputText = SkColorSetRGB(0xED, 0xF2, 0xF5);

constexpr SkColor kRowHoverBg = SkColorSetARGB(0x18, 0xFF, 0xFF, 0xFF);
constexpr SkColor kRowSelectedBg = SkColorSetARGB(0x30, 0x6E, 0xA1, 0xF7);
constexpr SkColor kRowSelectedBorder = SkColorSetARGB(0x55, 0x6E, 0xA1, 0xF7);
constexpr SkColor kTitleColor = SkColorSetRGB(0xE0, 0xE4, 0xE8);
constexpr SkColor kDomainColor = SkColorSetARGB(0x77, 0xED, 0xF2, 0xF5);

constexpr int kBackdropBlurRadius = 12;
constexpr float kBackdropBlurQuality = 0.33f;
constexpr int kRowHeight = 44;
constexpr int kRowCornerRadius = 8;
constexpr int kFaviconSize = 16;
constexpr int kInputFaviconSize = 16;
constexpr int kRowIconGap = 12;
constexpr int kListTopPadding = 16;

// ── Helpers ─────────────────────────────────────────────────────────────────

std::string StripUrlPrefix(const std::string& url_str) {
  GURL url(url_str);
  if (!url.is_valid()) {
    return url_str;
  }
  std::string host(url.host());
  if (base::StartsWith(host, "www.", base::CompareCase::INSENSITIVE_ASCII)) {
    host = host.substr(4);
  }
  return host;
}

// ── Rounded-rect background painter ─────────────────────────────────────────

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

// ── Input wrapper ───────────────────────────────────────────────────────────

class InputWrapper : public views::View {
  METADATA_HEADER(InputWrapper, views::View)

 public:
  InputWrapper() {
    SetBackground(std::make_unique<RoundedBackground>(
        kInputBg, kInputBorderFocused,
        AvoraQuickNavView::kInputCornerRadius));

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(0, 10), kRowIconGap));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
  }

  ~InputWrapper() override = default;
};

BEGIN_METADATA(InputWrapper)
END_METADATA

// ── History row (favicon | title | domain) ──────────────────────────────────

class HistoryRowView : public views::Button {
  METADATA_HEADER(HistoryRowView, views::Button)

 public:
  HistoryRowView(const std::string& url,
                 const std::string& title,
                 base::RepeatingCallback<void(const std::string&)> on_click)
      : url_(url) {
    SetCallback(base::BindRepeating(std::move(on_click), url));
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 0)));

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(4, 10), kRowIconGap));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    // Favicon — plain 32px ImageView, no background container.
    auto icon = std::make_unique<views::ImageView>();
    icon->SetImageSize(gfx::Size(kFaviconSize, kFaviconSize));
    icon->SetPreferredSize(gfx::Size(kFaviconSize, kFaviconSize));
    icon->SetHorizontalAlignment(views::ImageView::Alignment::kCenter);
    icon->SetVerticalAlignment(views::ImageView::Alignment::kCenter);
    icon_view_ = AddChildView(std::move(icon));

    // Title label.
    std::string display_title = title.empty() ? StripUrlPrefix(url) : title;
    auto title_label = std::make_unique<views::Label>(
        base::UTF8ToUTF16(display_title),
        views::Label::CustomFont{gfx::FontList({std::string("system-ui")},
                                                gfx::Font::NORMAL, 13,
                                                gfx::Font::Weight::MEDIUM)});
    title_label->SetEnabledColor(kTitleColor);
    title_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_label->SetElideBehavior(gfx::ELIDE_TAIL);
    title_label_ = AddChildView(std::move(title_label));
    layout->SetFlexForView(title_label_, 1);

    // Domain label.
    std::string domain = StripUrlPrefix(url);
    auto domain_label = std::make_unique<views::Label>(
        base::UTF8ToUTF16(domain),
        views::Label::CustomFont{gfx::FontList({std::string("system-ui")},
                                                gfx::Font::NORMAL, 12,
                                                gfx::Font::Weight::NORMAL)});
    domain_label->SetEnabledColor(kDomainColor);
    domain_label->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
    domain_label_ = AddChildView(std::move(domain_label));

    SetPreferredSize(gfx::Size(0, kRowHeight));
    SetAccessibleName(base::UTF8ToUTF16(display_title + " \u2014 " + domain));
  }

  ~HistoryRowView() override = default;

  const std::string& url() const { return url_; }

  void SetFavicon(const gfx::ImageSkia& icon) {
    if (icon.isNull() || !icon_view_) {
      return;
    }
    favicon_ = icon;
    gfx::ImageSkia resized = gfx::ImageSkiaOperations::CreateResizedImage(
        icon, skia::ImageOperations::RESIZE_BEST,
        gfx::Size(kFaviconSize, kFaviconSize));
    icon_view_->SetImage(ui::ImageModel::FromImageSkia(resized));
  }

  const gfx::ImageSkia& favicon() const { return favicon_; }

  void SetSelected(bool selected) {
    if (selected_ != selected) {
      selected_ = selected;
      SchedulePaint();
    }
  }

  bool selected() const { return selected_; }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const float r = static_cast<float>(kRowCornerRadius);
    if (selected_) {
      cc::PaintFlags fill;
      fill.setAntiAlias(true);
      fill.setColor(kRowSelectedBg);
      fill.setStyle(cc::PaintFlags::kFill_Style);
      canvas->DrawRoundRect(GetLocalBounds(), r, fill);

      cc::PaintFlags border;
      border.setAntiAlias(true);
      border.setColor(kRowSelectedBorder);
      border.setStyle(cc::PaintFlags::kStroke_Style);
      border.setStrokeWidth(1.0f);
      gfx::RectF br(GetLocalBounds());
      br.Inset(0.5f);
      canvas->DrawRoundRect(br, r, border);
    } else if (GetState() == views::Button::STATE_HOVERED ||
               GetState() == views::Button::STATE_PRESSED) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(kRowHoverBg);
      flags.setStyle(cc::PaintFlags::kFill_Style);
      canvas->DrawRoundRect(GetLocalBounds(), r, flags);
    }
  }

 private:
  std::string url_;
  gfx::ImageSkia favicon_;
  bool selected_ = false;
  raw_ptr<views::ImageView> icon_view_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> domain_label_ = nullptr;
};

BEGIN_METADATA(HistoryRowView)
END_METADATA

// ── Suggestion row (search icon | suggestion text) ──────────────────────────

class SuggestionRowView : public views::Button {
  METADATA_HEADER(SuggestionRowView, views::Button)

 public:
  SuggestionRowView(
      const std::string& text,
      base::RepeatingCallback<void(const std::string&)> on_click)
      : value_(text) {
    SetCallback(base::BindRepeating(std::move(on_click), text));
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 0)));

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(4, 10), kRowIconGap));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    // Search icon.
    auto icon = std::make_unique<views::ImageView>();
    icon->SetImageSize(gfx::Size(kFaviconSize, kFaviconSize));
    icon->SetPreferredSize(gfx::Size(kFaviconSize, kFaviconSize));
    icon->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kSearchIcon, kDomainColor, kFaviconSize));
    AddChildView(std::move(icon));

    // Suggestion text.
    auto label = std::make_unique<views::Label>(
        base::UTF8ToUTF16(text),
        views::Label::CustomFont{gfx::FontList({std::string("system-ui")},
                                                gfx::Font::NORMAL, 13,
                                                gfx::Font::Weight::MEDIUM)});
    label->SetEnabledColor(kTitleColor);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetElideBehavior(gfx::ELIDE_TAIL);
    auto* label_ptr = AddChildView(std::move(label));
    layout->SetFlexForView(label_ptr, 1);

    SetPreferredSize(gfx::Size(0, kRowHeight));
    SetAccessibleName(base::UTF8ToUTF16(text));
  }

  ~SuggestionRowView() override = default;

  const std::string& value() const { return value_; }

  void SetSelected(bool selected) {
    if (selected_ != selected) {
      selected_ = selected;
      SchedulePaint();
    }
  }

  bool selected() const { return selected_; }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const float r = static_cast<float>(kRowCornerRadius);
    if (selected_) {
      cc::PaintFlags fill;
      fill.setAntiAlias(true);
      fill.setColor(kRowSelectedBg);
      fill.setStyle(cc::PaintFlags::kFill_Style);
      canvas->DrawRoundRect(GetLocalBounds(), r, fill);

      cc::PaintFlags border;
      border.setAntiAlias(true);
      border.setColor(kRowSelectedBorder);
      border.setStyle(cc::PaintFlags::kStroke_Style);
      border.setStrokeWidth(1.0f);
      gfx::RectF br(GetLocalBounds());
      br.Inset(0.5f);
      canvas->DrawRoundRect(br, r, border);
    } else if (GetState() == views::Button::STATE_HOVERED ||
               GetState() == views::Button::STATE_PRESSED) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(kRowHoverBg);
      flags.setStyle(cc::PaintFlags::kFill_Style);
      canvas->DrawRoundRect(GetLocalBounds(), r, flags);
    }
  }

 private:
  std::string value_;
  bool selected_ = false;
};

BEGIN_METADATA(SuggestionRowView)
END_METADATA

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// AvoraQuickNavView
// ═══════════════════════════════════════════════════════════════════════════

AvoraQuickNavView::AvoraQuickNavView(BrowserWindowInterface* browser,
                                     avora::WindowSpaceState* window_space_state,
                                     QuickNavCommitCallback commit_cb,
                                     QuickNavDismissCallback dismiss_cb)
    : browser_(browser),
      commit_cb_(std::move(commit_cb)),
      dismiss_cb_(std::move(dismiss_cb)),
      window_space_state_(window_space_state) {
  // The overlay itself needs a layer for proper event handling.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);

  // Backdrop child: gets its own compositing layer with blur.
  // No layout manager — we position backdrop_ and card_ manually.
  auto backdrop = std::make_unique<views::View>();
  backdrop->SetPaintToLayer();
  backdrop->layer()->SetFillsBoundsOpaquely(false);
  backdrop->layer()->SetBackgroundBlur(kBackdropBlurRadius);
  backdrop->layer()->SetBackdropFilterQuality(kBackdropBlurQuality);
  backdrop->SetBackground(views::CreateSolidBackground(kBackdropColor));
  // Let mouse events pass through to the parent overlay for dismiss handling.
  backdrop->SetCanProcessEventsWithinSubtree(false);
  backdrop_ = AddChildView(std::move(backdrop));

  SetFocusBehavior(FocusBehavior::ALWAYS);
  SetAccessibleRole(ax::mojom::Role::kDialog);
  SetAccessibleName(u"Quick navigation overlay");
  SetVisible(false);

  Profile* profile = browser_->GetProfile();
  favorites_manager_ =
      std::make_unique<FavoritesManager>(profile->GetPrefs());
  favorites_manager_->AddObserver(this);
  if (window_space_state_) {
    favorites_manager_->SetWindowActiveSpaceId(
        window_space_state_->active_space_id());
  }

  suggest_provider_ = std::make_unique<SearchSuggestProvider>(
      profile->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess());

  BuildCard();
}

AvoraQuickNavView::~AvoraQuickNavView() {
  if (favorites_manager_) {
    favorites_manager_->RemoveObserver(this);
  }
}

// ── Public API ──────────────────────────────────────────────────────────────

void AvoraQuickNavView::Show(const std::string& current_page_url) {
  // CMD+T already created a new tab before showing this overlay,
  // so navigate that tab in-place rather than opening yet another one.
  open_in_new_tab_ = false;
  current_page_url_ = current_page_url;
  ShowOverlay(std::u16string());
}

void AvoraQuickNavView::ShowWithURL(const std::u16string& url_text,
                                    const std::string& current_page_url) {
  open_in_new_tab_ = false;
  current_page_url_ = current_page_url;
  ShowOverlay(url_text);
}

void AvoraQuickNavView::ShowOverlay(const std::u16string& url_text) {
  SetVisible(true);
  RebuildHistoryList();
  LayoutCard();

  // Reset inline icon to search icon; it will be replaced by a favicon
  // when the user selects a row from the list.
  if (input_favicon_) {
    input_favicon_->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kSearchIcon, kDomainColor, kInputFaviconSize));
    input_favicon_->SetVisible(true);
    // Only load a page favicon for CMD+L (url_text is pre-filled).
    // CMD+T should keep the search icon in the empty input field.
    if (!url_text.empty() && !current_page_url_.empty()) {
      LoadInputFavicon(current_page_url_);
    }
  }

  if (input_field_) {
    updating_from_selection_ = true;
    input_field_->SetText(url_text);
    if (!url_text.empty()) {
      input_field_->SelectAll(true);
    }
    input_field_->RequestFocus();
    updating_from_selection_ = false;
  }

  // CMD+L: pre-select the first row (current URL) and highlight it.
  // CMD+T: leave input empty with placeholder, no row selected.
  if (!url_text.empty() && !row_urls_.empty()) {
    UpdateSelection(0);
  }
}

void AvoraQuickNavView::Hide() {
  SetVisible(false);
  selected_index_ = -1;
  row_urls_.clear();
  current_page_url_.clear();
  showing_suggestions_ = false;
  suggest_timer_.Stop();
  if (suggest_provider_) {
    suggest_provider_->Cancel();
  }
  if (input_field_) {
    input_field_->SetText(std::u16string());
  }
  if (input_favicon_) {
    input_favicon_->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kSearchIcon, kDomainColor, kInputFaviconSize));
    input_favicon_->SetVisible(true);
  }
  cancelable_task_tracker_.TryCancelAll();
}

bool AvoraQuickNavView::IsShowing() const {
  return GetVisible();
}

// ── View overrides ──────────────────────────────────────────────────────────

bool AvoraQuickNavView::OnMousePressed(const ui::MouseEvent& event) {
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
  if (input_field_) {
    input_field_->RequestFocus();
  }
  return true;
}

bool AvoraQuickNavView::OnKeyPressed(const ui::KeyEvent& event) {
  if (event.key_code() == ui::VKEY_ESCAPE) {
    if (dismiss_cb_) {
      dismiss_cb_.Run();
    }
    return true;
  }
  return false;
}

void AvoraQuickNavView::VisibilityChanged(views::View* starting_from,
                                          bool is_visible) {
  if (is_visible && starting_from == this && input_field_) {
    input_field_->RequestFocus();
  }
}

void AvoraQuickNavView::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  LayoutCard();
}

// ── TextfieldController ─────────────────────────────────────────────────────

bool AvoraQuickNavView::HandleKeyEvent(views::Textfield* sender,
                                       const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }

  if (key_event.key_code() == ui::VKEY_DOWN) {
    MoveSelection(1);
    return true;
  }
  if (key_event.key_code() == ui::VKEY_UP) {
    MoveSelection(-1);
    return true;
  }

  if (key_event.key_code() == ui::VKEY_RETURN) {
    std::u16string text(sender->GetText());
    if (!text.empty() && commit_cb_) {
      commit_cb_.Run(text, open_in_new_tab_);
    }
    if (dismiss_cb_) {
      dismiss_cb_.Run();
    }
    return true;
  }

  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    if (dismiss_cb_) {
      dismiss_cb_.Run();
    }
    return true;
  }

  // Any other key: clear selection so the user is typing freely.
  if (selected_index_ >= 0 && !updating_from_selection_) {
    UpdateSelection(-1);
  }

  return false;
}

void AvoraQuickNavView::ContentsChanged(views::Textfield* sender,
                                        const std::u16string& new_contents) {
  if (updating_from_selection_) {
    return;
  }

  std::string query = base::UTF16ToUTF8(new_contents);

  if (query.empty()) {
    suggest_timer_.Stop();
    if (suggest_provider_) {
      suggest_provider_->Cancel();
    }
    showing_suggestions_ = false;
    RebuildHistoryList();
    if (input_favicon_) {
      input_favicon_->SetImage(ui::ImageModel::FromVectorIcon(
          vector_icons::kSearchIcon, kDomainColor, kInputFaviconSize));
    }
    return;
  }

  suggest_timer_.Start(
      FROM_HERE, base::Milliseconds(200),
      base::BindOnce(&AvoraQuickNavView::FetchSuggestions,
                     weak_factory_.GetWeakPtr(), query));
}

// ── FavoritesManager::Observer ──────────────────────────────────────────────

void AvoraQuickNavView::OnFavoritesChanged() {
  if (IsShowing() && !showing_suggestions_) {
    RebuildHistoryList();
  }
}

// ── Private helpers ─────────────────────────────────────────────────────────

void AvoraQuickNavView::BuildCard() {
  auto card = std::make_unique<views::View>();

  card->SetPaintToLayer();
  card->layer()->SetFillsBoundsOpaquely(false);
  card->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(kCardCornerRadius));

  card->SetBackground(std::make_unique<RoundedBackground>(
      kCardBg, kCardBorder, kCardCornerRadius));

  auto* card_layout =
      card->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical,
          gfx::Insets::TLBR(kCardPaddingTop, kCardPaddingH,
                            kCardPaddingBottom, kCardPaddingH)));
  card_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // ── Input field ──
  auto input_wrapper = std::make_unique<InputWrapper>();
  input_wrapper->SetPreferredSize(gfx::Size(0, kInputHeight));

  // Inline icon (defaults to search icon, replaced by favicon when navigating).
  auto input_icon = std::make_unique<views::ImageView>();
  input_icon->SetImageSize(gfx::Size(kInputFaviconSize, kInputFaviconSize));
  input_icon->SetPreferredSize(gfx::Size(kInputFaviconSize, kInputFaviconSize));
  input_icon->SetImage(ui::ImageModel::FromVectorIcon(
      vector_icons::kSearchIcon, kDomainColor, kInputFaviconSize));
  input_icon->SetVisible(true);
  input_favicon_ = input_wrapper->AddChildView(std::move(input_icon));

  auto field = std::make_unique<views::Textfield>();
  field->SetFontList(gfx::FontList({std::string("system-ui")},
                                    gfx::Font::NORMAL, 15,
                                    gfx::Font::Weight::NORMAL));
  field->SetColor(kInputText);
  field->SetBackgroundColor(kInputBg);
  field->SetBorder(nullptr);
  field->SetPlaceholderText(u"Search or enter a URL\u2026");
  field->set_controller(this);

  input_field_ = input_wrapper->AddChildView(std::move(field));

  auto* wrapper_layout = static_cast<views::BoxLayout*>(
      input_wrapper->GetLayoutManager());
  wrapper_layout->SetFlexForView(input_field_, 1);

  card->AddChildView(std::move(input_wrapper));

  // ── History list container ──
  auto list = std::make_unique<views::View>();
  list->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(kListTopPadding, 0, 0, 0), 2));
  grid_container_ = card->AddChildView(std::move(list));

  card_ = AddChildView(std::move(card));
}

void AvoraQuickNavView::RebuildHistoryList() {
  if (!grid_container_ || !favorites_manager_) {
    return;
  }

  grid_container_->RemoveAllChildViews();
  row_urls_.clear();
  selected_index_ = -1;
  showing_suggestions_ = false;

  auto on_click = base::BindRepeating(&AvoraQuickNavView::OnFavoriteClicked,
                                      base::Unretained(this));

  // 1. Current page URL as the first entry.
  if (!current_page_url_.empty()) {
    GURL current_gurl(current_page_url_);
    std::string title;
    if (current_gurl.is_valid()) {
      title = StripUrlPrefix(current_page_url_);
      if (!title.empty()) {
        title[0] = base::ToUpperASCII(title[0]);
      }
    }
    auto* row = grid_container_->AddChildView(
        std::make_unique<HistoryRowView>(current_page_url_, title, on_click));
    LoadFavicon(row, current_page_url_);
    row_urls_.push_back(current_page_url_);
  }

  // 2. Favorites (skip duplicate of current URL).
  auto favorites = favorites_manager_->GetFavorites();
  for (const auto& entry : favorites) {
    if (entry.url == current_page_url_) {
      continue;
    }
    auto* row = grid_container_->AddChildView(
        std::make_unique<HistoryRowView>(entry.url, entry.title, on_click));
    LoadFavicon(row, entry.url);
    row_urls_.push_back(entry.url);
  }

  grid_container_->SetVisible(!row_urls_.empty());
  grid_container_->InvalidateLayout();
  if (card_) {
    card_->InvalidateLayout();
  }
}

void AvoraQuickNavView::UpdateSelection(int new_index) {
  if (!grid_container_) {
    return;
  }

  int count = static_cast<int>(grid_container_->children().size());
  if (count == 0) {
    selected_index_ = -1;
    return;
  }

  new_index = std::max(-1, std::min(new_index, count - 1));

  // Clear previous selection.
  if (selected_index_ >= 0 && selected_index_ < count) {
    if (showing_suggestions_) {
      static_cast<SuggestionRowView*>(
          grid_container_->children()[selected_index_])
          ->SetSelected(false);
    } else {
      static_cast<HistoryRowView*>(
          grid_container_->children()[selected_index_])
          ->SetSelected(false);
    }
  }

  selected_index_ = new_index;

  // Apply new selection and update input field + favicon.
  if (selected_index_ >= 0 && selected_index_ < count) {
    if (showing_suggestions_) {
      static_cast<SuggestionRowView*>(
          grid_container_->children()[selected_index_])
          ->SetSelected(true);
    } else {
      static_cast<HistoryRowView*>(
          grid_container_->children()[selected_index_])
          ->SetSelected(true);
    }

    if (input_field_ && selected_index_ < static_cast<int>(row_urls_.size())) {
      updating_from_selection_ = true;
      input_field_->SetText(base::UTF8ToUTF16(row_urls_[selected_index_]));
      input_field_->SelectAll(true);
      updating_from_selection_ = false;
    }

    // Update the input favicon.
    if (input_favicon_) {
      if (showing_suggestions_) {
        input_favicon_->SetImage(ui::ImageModel::FromVectorIcon(
            vector_icons::kSearchIcon, kDomainColor, kInputFaviconSize));
        input_favicon_->SetVisible(true);
      } else {
        auto* row = static_cast<HistoryRowView*>(
            grid_container_->children()[selected_index_]);
        const gfx::ImageSkia& row_icon = row->favicon();
        if (!row_icon.isNull()) {
          gfx::ImageSkia resized =
              gfx::ImageSkiaOperations::CreateResizedImage(
                  row_icon, skia::ImageOperations::RESIZE_BEST,
                  gfx::Size(kInputFaviconSize, kInputFaviconSize));
          input_favicon_->SetImage(ui::ImageModel::FromImageSkia(resized));
          input_favicon_->SetVisible(true);
        } else {
          input_favicon_->SetImage(ui::ImageModel::FromVectorIcon(
              vector_icons::kSearchIcon, kDomainColor, kInputFaviconSize));
          input_favicon_->SetVisible(true);
        }
      }
    }
  } else {
    // No selection — revert to search icon.
    if (input_favicon_) {
      input_favicon_->SetImage(ui::ImageModel::FromVectorIcon(
          vector_icons::kSearchIcon, kDomainColor, kInputFaviconSize));
      input_favicon_->SetVisible(true);
    }
  }
}

void AvoraQuickNavView::MoveSelection(int delta) {
  int count = static_cast<int>(row_urls_.size());
  if (count == 0) {
    return;
  }

  int new_index = selected_index_ + delta;
  if (new_index < 0) {
    new_index = count - 1;
  } else if (new_index >= count) {
    new_index = 0;
  }

  UpdateSelection(new_index);
}

void AvoraQuickNavView::LayoutCard() {
  if (!card_) {
    return;
  }

  const gfx::Size host = size();
  if (host.IsEmpty()) {
    return;
  }

  // Backdrop fills the entire overlay, with rounded bottom corners matching the
  // macOS window.
  if (backdrop_) {
    backdrop_->SetBoundsRect(gfx::Rect(host));
    constexpr float kWindowRadius = 10.0f;
    backdrop_->layer()->SetRoundedCornerRadius(
        gfx::RoundedCornersF(kWindowRadius, kWindowRadius,
                              kWindowRadius, kWindowRadius));
    backdrop_->layer()->SetIsFastRoundedCorner(true);
  }

  int card_width = std::min(host.width() - 64, kCardMaxWidth);
  int card_x = (host.width() - card_width) / 2;

  int content_height = kCardPaddingTop + kInputHeight + kCardPaddingBottom;
  if (grid_container_ && grid_container_->GetVisible()) {
    int num_rows = static_cast<int>(grid_container_->children().size());
    int list_height = kListTopPadding + num_rows * (kRowHeight + 2);
    content_height += list_height;
  }

  int max_card_height = host.height() - 80;
  int card_height = std::min(content_height, max_card_height);
  card_height = std::max(card_height,
                         kCardPaddingTop + kInputHeight + kCardPaddingBottom);

  int card_y = (host.height() - card_height) * 2 / 5;
  card_y = std::max(card_y, 40);

  card_->SetBoundsRect(
      gfx::Rect(card_x, card_y, card_width, card_height));
  card_->DeprecatedLayoutImmediately();
}

void AvoraQuickNavView::LoadFavicon(views::View* row_view,
                                    const std::string& url) {
  if (!browser_) {
    return;
  }
  favicon::FaviconService* favicon_service =
      FaviconServiceFactory::GetForProfile(
          browser_->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
  if (!favicon_service) {
    return;
  }

  favicon_service->GetFaviconImageForPageURL(
      GURL(url),
      base::BindOnce(
          [](base::WeakPtr<AvoraQuickNavView> view, const std::string& url,
             const favicon_base::FaviconImageResult& result) {
            if (!view || !view->grid_container_ || view->showing_suggestions_) {
              return;
            }
            for (views::View* child : view->grid_container_->children()) {
              auto* row = static_cast<HistoryRowView*>(child);
              if (row && row->url() == url) {
                row->SetFavicon(result.image.AsImageSkia());
                break;
              }
            }
          },
          weak_factory_.GetWeakPtr(), url),
      &cancelable_task_tracker_);
}

void AvoraQuickNavView::LoadInputFavicon(const std::string& url) {
  if (!browser_ || !input_favicon_) {
    return;
  }
  favicon::FaviconService* favicon_service =
      FaviconServiceFactory::GetForProfile(
          browser_->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
  if (!favicon_service) {
    return;
  }

  favicon_service->GetFaviconImageForPageURL(
      GURL(url),
      base::BindOnce(
          [](base::WeakPtr<AvoraQuickNavView> view,
             const favicon_base::FaviconImageResult& result) {
            if (!view || !view->input_favicon_) {
              return;
            }
            if (!result.image.IsEmpty()) {
              gfx::ImageSkia icon = result.image.AsImageSkia();
              gfx::ImageSkia resized =
                  gfx::ImageSkiaOperations::CreateResizedImage(
                      icon, skia::ImageOperations::RESIZE_BEST,
                      gfx::Size(kInputFaviconSize, kInputFaviconSize));
              view->input_favicon_->SetImage(
                  ui::ImageModel::FromImageSkia(resized));
              view->input_favicon_->SetVisible(true);
            }
          },
          weak_factory_.GetWeakPtr()),
      &cancelable_task_tracker_);
}

void AvoraQuickNavView::OnFavoriteClicked(const std::string& url) {
  if (commit_cb_) {
    commit_cb_.Run(base::UTF8ToUTF16(url), open_in_new_tab_);
  }
  if (dismiss_cb_) {
    dismiss_cb_.Run();
  }
}

void AvoraQuickNavView::FetchSuggestions(const std::string& query) {
  if (!suggest_provider_) {
    return;
  }
  suggest_provider_->FetchSuggestions(
      query, base::BindOnce(&AvoraQuickNavView::OnSuggestionsReceived,
                            weak_factory_.GetWeakPtr(), query));
}

void AvoraQuickNavView::OnSuggestionsReceived(
    const std::string& query,
    std::vector<std::string> suggestions) {
  if (!IsShowing() || !input_field_) {
    return;
  }

  // Discard stale results if the user has since changed the input.
  std::string current_text = base::UTF16ToUTF8(input_field_->GetText());
  if (current_text != query) {
    return;
  }

  RebuildSuggestionList(query, suggestions);
}

void AvoraQuickNavView::RebuildSuggestionList(
    const std::string& query,
    const std::vector<std::string>& suggestions) {
  if (!grid_container_) {
    return;
  }

  grid_container_->RemoveAllChildViews();
  row_urls_.clear();
  selected_index_ = -1;
  showing_suggestions_ = true;

  // Cancel any outstanding favicon fetches from the previous favorites list.
  cancelable_task_tracker_.TryCancelAll();

  auto on_click = base::BindRepeating(&AvoraQuickNavView::OnFavoriteClicked,
                                      base::Unretained(this));

  // First row: the typed query itself (so Enter always searches for it).
  grid_container_->AddChildView(
      std::make_unique<SuggestionRowView>(query, on_click));
  row_urls_.push_back(query);

  // Remaining rows: API suggestions (skip duplicates of the query).
  constexpr size_t kMaxSuggestions = 8;
  for (const auto& suggestion : suggestions) {
    if (row_urls_.size() >= kMaxSuggestions) {
      break;
    }
    if (suggestion == query) {
      continue;
    }
    grid_container_->AddChildView(
        std::make_unique<SuggestionRowView>(suggestion, on_click));
    row_urls_.push_back(suggestion);
  }

  grid_container_->SetVisible(!row_urls_.empty());
  grid_container_->InvalidateLayout();
  if (card_) {
    card_->InvalidateLayout();
  }
  LayoutCard();
}

BEGIN_METADATA(AvoraQuickNavView)
END_METADATA

}  // namespace avora
