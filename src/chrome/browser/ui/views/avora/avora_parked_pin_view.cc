// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_parked_pin_view.h"

#include <algorithm>
#include <iterator>
#include <memory>

#include "base/rand_util.h"
#include "base/supports_user_data.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace avora {

namespace {

const char kParkedPinTabKey[] = "avora_parked_pin_tab";

struct Quote {
  const char16_t* text;
  const char16_t* by;
};

// Half borrowed wisdom, half the browser having opinions.  Short on purpose:
// these are read at a glance, at display size.
constexpr Quote kQuotes[] = {
    {u"Simplicity is the ultimate sophistication.", u"Leonardo da Vinci"},
    {u"Well done is better than well said.", u"Benjamin Franklin"},
    {u"Nothing is permanent except change.", u"Heraclitus"},
    {u"The secret of getting ahead is getting started.", u"Mark Twain"},
    {u"Whatever you are, be a good one.", u"Abraham Lincoln"},
    {u"It does not matter how slowly you go as long as you do not stop.",
     u"Confucius"},
    {u"Out of sight, out of mind. Still pinned, though.", u"Avora"},
    {u"This pin is load-bearing. It stays.", u"Avora"},
    {u"Closed in spirit. Open in fact.", u"Avora"},
    {u"You can minimize it. You can't make it leave.", u"Avora"},
    {u"Nothing to see here. Literally. That was the point.", u"Avora"},
    {u"Some tabs are just pinned for life.", u"Avora"},
};

// The content card's own colouring and corner, so the quote reads as the page
// having been set aside rather than as a dialog on top of it.
constexpr SkColor kBackgroundColor = SkColorSetRGB(0x16, 0x19, 0x1E);
constexpr SkColor kQuoteColor = SkColorSetRGB(0xED, 0xF2, 0xF5);
constexpr SkColor kAttributionColor = SkColorSetARGB(0x99, 0xED, 0xF2, 0xF5);
constexpr float kCornerRadius = 10.0f;

// Display sizes: the quote is the only thing on screen, so it is set large
// enough to read from across the desk.
constexpr int kQuoteFontSize = 34;
constexpr int kAttributionFontSize = 20;
constexpr int kMaxTextWidth = 720;
constexpr int kMinSideMargin = 48;

class ParkedPinTabData : public base::SupportsUserData::Data {
 public:
  explicit ParkedPinTabData(size_t quote_index) : quote_index_(quote_index) {}
  ~ParkedPinTabData() override = default;

  // Picked once, when the tab is parked, so the quote does not change under
  // the user every time they switch away and back.
  size_t quote_index() const { return quote_index_; }

 private:
  size_t quote_index_;
};

ParkedPinTabData* GetData(content::WebContents* contents) {
  if (!contents) {
    return nullptr;
  }
  return static_cast<ParkedPinTabData*>(
      contents->GetUserData(kParkedPinTabKey));
}

void NotifyChanged(TabStripModel* tab_strip, content::WebContents* contents) {
  if (tab_strip &&
      tab_strip->GetIndexOfWebContents(contents) != TabStripModel::kNoTab) {
    tab_strip->UpdateWebContentsState(contents, TabChangeType::kAll);
  }
}

gfx::FontList DisplayFont(int size, gfx::Font::Weight weight) {
  return gfx::FontList({std::string("system-ui")}, gfx::Font::NORMAL, size,
                       weight);
}

}  // namespace

void ParkPinnedTab(TabStripModel* tab_strip, content::WebContents* contents) {
  if (!contents || GetData(contents)) {
    return;
  }
  const size_t index =
      static_cast<size_t>(base::RandGenerator(std::size(kQuotes)));
  contents->SetUserData(kParkedPinTabKey,
                        std::make_unique<ParkedPinTabData>(index));
  NotifyChanged(tab_strip, contents);
}

void UnparkPinnedTab(TabStripModel* tab_strip, content::WebContents* contents) {
  if (!GetData(contents)) {
    return;
  }
  contents->RemoveUserData(kParkedPinTabKey);
  NotifyChanged(tab_strip, contents);
}

bool IsPinnedTabParked(content::WebContents* contents) {
  return GetData(contents) != nullptr;
}

AvoraParkedPinView::AvoraParkedPinView(TabStripModel* tab_strip)
    : tab_strip_(tab_strip) {
  // Its own layer, so it composites above the page's.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(kCornerRadius));
  layer()->SetIsFastRoundedCorner(true);

  auto column = std::make_unique<views::View>();
  auto* column_layout = column->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 20));
  column_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto quote = std::make_unique<views::Label>(
      std::u16string(),
      views::Label::CustomFont{
          DisplayFont(kQuoteFontSize, gfx::Font::Weight::MEDIUM)});
  quote->SetMultiLine(true);
  quote->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  quote->SetEnabledColor(kQuoteColor);
  quote->SetAutoColorReadabilityEnabled(false);
  quote_label_ = column->AddChildView(std::move(quote));

  auto attribution = std::make_unique<views::Label>(
      std::u16string(),
      views::Label::CustomFont{
          DisplayFont(kAttributionFontSize, gfx::Font::Weight::NORMAL)});
  attribution->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  attribution->SetEnabledColor(kAttributionColor);
  attribution->SetAutoColorReadabilityEnabled(false);
  attribution_label_ = column->AddChildView(std::move(attribution));

  text_column_ = AddChildView(std::move(column));

  SetVisible(false);
  if (tab_strip_) {
    tab_strip_->AddObserver(this);
  }
  Refresh();
}

AvoraParkedPinView::~AvoraParkedPinView() {
  if (tab_strip_) {
    tab_strip_->RemoveObserver(this);
  }
}

void AvoraParkedPinView::OnPaintBackground(gfx::Canvas* canvas) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(kBackgroundColor);
  canvas->DrawRect(GetLocalBounds(), flags);
}

void AvoraParkedPinView::Layout(PassKey) {
  // A centred column, narrow enough to read as a line of text rather than a
  // banner, sitting a little above the optical middle.
  const int width =
      std::max(0, std::min(kMaxTextWidth, this->width() - 2 * kMinSideMargin));
  const int height = text_column_->GetHeightForWidth(width);
  text_column_->SetBounds((this->width() - width) / 2,
                          std::max(0, (this->height() - height) * 2 / 5), width,
                          height);
}

void AvoraParkedPinView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  Refresh();
}

void AvoraParkedPinView::OnTabChangedAt(tabs::TabInterface* tab,
                                        TabChangeType change_type) {
  if (change_type == TabChangeType::kAll) {
    Refresh();
  }
}

void AvoraParkedPinView::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
  // Going somewhere -- the address bar, Back, a reload -- is asking for a page,
  // so the parked tab wakes up rather than loading behind the quote.
  if (!navigation_handle->IsInPrimaryMainFrame() ||
      navigation_handle->IsSameDocument()) {
    return;
  }
  // Notifies the tab strip, which comes back through Refresh() and hides this.
  UnparkPinnedTab(tab_strip_, web_contents());
}

void AvoraParkedPinView::Refresh() {
  content::WebContents* active =
      tab_strip_ ? tab_strip_->GetActiveWebContents() : nullptr;
  ParkedPinTabData* data = GetData(active);
  Observe(data ? active : nullptr);

  if (data) {
    const Quote& quote = kQuotes[data->quote_index() % std::size(kQuotes)];
    quote_label_->SetText(std::u16string(u"“") + quote.text + u"”");
    attribution_label_->SetText(std::u16string(u"— ") + quote.by);
  }
  if (GetVisible() != !!data) {
    SetVisible(!!data);
  }
  InvalidateLayout();
}

bool AvoraParkedPinView::OnMousePressed(const ui::MouseEvent& event) {
  // Swallow clicks: the page behind is set aside, not merely dimmed.
  return true;
}

BEGIN_METADATA(AvoraParkedPinView)
END_METADATA

}  // namespace avora
