// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_imported_section_view.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/pickle.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/avora/avora_favorites.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/views/avora/avora_import_dialog_view.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/views/widget/widget.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "url/gurl.h"

namespace avora {

// Static custom clipboard format for imported link drag identity.
const ui::ClipboardFormatType& GetImportedLinkClipboardFormatType() {
  static const base::NoDestructor<ui::ClipboardFormatType> format(
      ui::ClipboardFormatType::CustomPlatformType("avora/imported-link"));
  return *format;
}

namespace {

// Minimum pixel distance before a mouse press becomes a drag.
constexpr int kMinDragDistance = 10;

// ── Context menu command IDs ─────────────────────────────────────────────────
constexpr int kMenuOpenId = 1;
constexpr int kMenuOpenBackgroundId = 2;
constexpr int kMenuCopyLinkId = 3;
constexpr int kMenuAddToFavoritesId = 4;
constexpr int kMenuRemoveFromImportedId = 5;

// Delegate that forwards command execution to a repeating callback.
class ImportedLinkMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  explicit ImportedLinkMenuDelegate(
      base::RepeatingCallback<void(int)> on_command)
      : on_command_(std::move(on_command)) {}

  void ExecuteCommand(int command_id, int event_flags) override {
    if (on_command_) {
      on_command_.Run(command_id);
    }
  }

 private:
  base::RepeatingCallback<void(int)> on_command_;
};

// ── Styling ─────────────────────────────────────────────────────────────────
// Deliberately muted relative to Favorites and Pinned.  The section header
// and source headers use smaller/lighter text; link rows use the standard
// favicon + title pattern at reduced height.

constexpr SkColor kRowHoverBg = SkColorSetARGB(0x1A, 0xFF, 0xFF, 0xFF);
constexpr SkColor kLabelColor = SkColorSetARGB(0xE0, 0xED, 0xF2, 0xF5);
constexpr SkColor kSubtleColor = SkColorSetARGB(0x80, 0xED, 0xF2, 0xF5);
constexpr SkColor kChevronColor = SkColorSetARGB(0xB0, 0xED, 0xF2, 0xF5);
constexpr SkColor kSourceLabelColor = SkColorSetARGB(0x99, 0xED, 0xF2, 0xF5);

constexpr int kHPadding = AvoraImportedSectionView::kHPadding;
constexpr int kFaviconSize = AvoraImportedSectionView::kFaviconSize;
constexpr int kRowHeight = AvoraImportedSectionView::kRowHeight;
constexpr int kHeaderHeight = AvoraImportedSectionView::kHeaderHeight;
constexpr int kSourceHeaderHeight = AvoraImportedSectionView::kSourceHeaderHeight;
constexpr int kIndentPerLevel = AvoraImportedSectionView::kIndentPerLevel;
constexpr int kChevronSize = 8;

std::string FolderKey(const std::string& source_id,
                      const std::string& item_id) {
  return source_id + ":" + item_id;
}

void PaintChevron(gfx::Canvas* canvas,
                  int x,
                  int y,
                  bool expanded) {
  SkPathBuilder builder;
  if (expanded) {
    // Down-pointing triangle.
    builder.moveTo(x, y);
    builder.lineTo(x + kChevronSize, y);
    builder.lineTo(x + kChevronSize / 2, y + kChevronSize * 0.6f);
  } else {
    // Right-pointing triangle.
    builder.moveTo(x, y);
    builder.lineTo(x + kChevronSize * 0.6f, y + kChevronSize / 2);
    builder.lineTo(x, y + kChevronSize);
  }
  builder.close();

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(kChevronColor);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawPath(builder.detach(), flags);
}

void PaintHoverBackground(gfx::Canvas* canvas,
                          const gfx::Rect& bounds) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(kRowHoverBg);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawRoundRect(gfx::RectF(bounds), 6.0f, flags);
}

}  // namespace

// ═════════════════════════════════════════════════════════════════════════════
// ImportedLinkRow
// ═════════════════════════════════════════════════════════════════════════════

ImportedLinkRow::ImportedLinkRow(const std::string& source_id,
                                 const std::string& item_id,
                                 const std::string& url,
                                 const std::string& title,
                                 int depth,
                                 ClickCallback on_click,
                                 ContextCallback on_context)
    : source_id_(source_id),
      item_id_(item_id),
      url_(url),
      title_(title),
      on_click_(std::move(on_click)),
      on_context_(std::move(on_context)) {
  SetPreferredSize(gfx::Size(0, kRowHeight));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(0, kHPadding + depth * kIndentPerLevel, 0, kHPadding),
      8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto favicon = std::make_unique<views::ImageView>();
  favicon->SetImageSize(gfx::Size(kFaviconSize, kFaviconSize));
  favicon_view_ = AddChildView(std::move(favicon));

  auto label = std::make_unique<views::Label>(
      base::UTF8ToUTF16(title.empty() ? url : title));
  label->SetEnabledColor(kLabelColor);
  label->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL, 12,
                                   gfx::Font::Weight::NORMAL));
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  title_label_ = AddChildView(std::move(label));
  layout->SetFlexForView(title_label_, 1);
}

ImportedLinkRow::~ImportedLinkRow() = default;

void ImportedLinkRow::SetFavicon(const gfx::ImageSkia& icon) {
  if (favicon_view_) {
    favicon_view_->SetImage(ui::ImageModel::FromImageSkia(
        gfx::ImageSkiaOperations::CreateResizedImage(
            icon, skia::ImageOperations::RESIZE_BEST,
            gfx::Size(kFaviconSize, kFaviconSize))));
  }
}

void ImportedLinkRow::OnPaintBackground(gfx::Canvas* canvas) {
  if (hovered_) {
    PaintHoverBackground(canvas, GetLocalBounds());
  }
}

bool ImportedLinkRow::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsRightMouseButton()) {
    if (on_context_) {
      gfx::Point screen_point = event.location();
      ConvertPointToScreen(this, &screen_point);
      on_context_.Run(source_id_, item_id_, url_, title_, screen_point);
    }
    return true;
  }
  if (event.IsMiddleMouseButton()) {
    if (on_click_) {
      on_click_.Run(url_, /*background=*/true);
    }
    return true;
  }
  if (event.IsOnlyLeftMouseButton()) {
    // Capture the press point for drag detection.  The click fires
    // in OnMouseReleased if the user doesn't exceed the drag threshold.
    left_pressed_ = true;
    drag_started_ = false;
    press_point_ = event.location();
    return true;
  }
  return false;
}

bool ImportedLinkRow::OnMouseDragged(const ui::MouseEvent& event) {
  if (!left_pressed_ || drag_started_) {
    return true;
  }
  MaybeStartDrag(event);
  return true;
}

void ImportedLinkRow::OnMouseReleased(const ui::MouseEvent& event) {
  if (left_pressed_ && !drag_started_ && event.IsOnlyLeftMouseButton()) {
    if (on_click_) {
      on_click_.Run(url_, /*background=*/false);
    }
  }
  left_pressed_ = false;
  drag_started_ = false;
}

void ImportedLinkRow::OnMouseCaptureLost() {
  left_pressed_ = false;
  drag_started_ = false;
}

void ImportedLinkRow::MaybeStartDrag(const ui::MouseEvent& event) {
  // Only real imported links (with a source_id and URL) are draggable.
  // Action rows like "Import Bookmarks…" are not.
  if (source_id_.empty() || url_.empty()) {
    return;
  }

  const int dx = event.location().x() - press_point_.x();
  const int dy = event.location().y() - press_point_.y();
  if (dx * dx + dy * dy < kMinDragDistance * kMinDragDistance) {
    return;
  }

  drag_started_ = true;

  auto data = std::make_unique<ui::OSExchangeData>();

  // Standard URL payload — allows the Favorites view's existing URL drop
  // path to work as a fallback and provides interoperability.
  data->SetURL(GURL(url_), base::UTF8ToUTF16(title_));

  // Custom pickled payload with stable identity (source_id + item_id).
  // The Favorites drop handler checks for this first.
  base::Pickle pickle;
  pickle.WriteString(source_id_);
  pickle.WriteString(item_id_);
  data->SetPickledData(GetImportedLinkClipboardFormatType(), pickle);

  views::Widget* widget = GetWidget();
  if (widget) {
    gfx::Point location = event.location();
    ConvertPointToWidget(this, &location);
    widget->RunDragDropLoop(
        this, std::move(data), location,
        ui::DragDropTypes::DRAG_COPY,
        ui::mojom::DragEventSource::kMouse);
  }
}

void ImportedLinkRow::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  SchedulePaint();
}

void ImportedLinkRow::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  SchedulePaint();
}

BEGIN_METADATA(ImportedLinkRow)
END_METADATA

// ═════════════════════════════════════════════════════════════════════════════
// ImportedFolderRow
// ═════════════════════════════════════════════════════════════════════════════

ImportedFolderRow::ImportedFolderRow(
    const std::string& source_id,
    const std::string& item_id,
    const std::string& title,
    bool expanded,
    int depth,
    base::RepeatingCallback<void(const std::string&, const std::string&)>
        on_toggle)
    : source_id_(source_id),
      item_id_(item_id),
      expanded_(expanded),
      on_toggle_(std::move(on_toggle)) {
  SetPreferredSize(gfx::Size(0, kRowHeight));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(0, kHPadding + depth * kIndentPerLevel, 0, kHPadding),
      6));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Chevron space is painted in OnPaintBackground.
  auto spacer = std::make_unique<views::View>();
  spacer->SetPreferredSize(gfx::Size(kChevronSize + 4, kChevronSize));
  AddChildView(std::move(spacer));

  auto label = std::make_unique<views::Label>(base::UTF8ToUTF16(title));
  label->SetEnabledColor(kLabelColor);
  label->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL, 12,
                                   gfx::Font::Weight::MEDIUM));
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  AddChildView(std::move(label));
  layout->SetFlexForView(children().back(), 1);
}

ImportedFolderRow::~ImportedFolderRow() = default;

void ImportedFolderRow::OnPaintBackground(gfx::Canvas* canvas) {
  if (hovered_) {
    PaintHoverBackground(canvas, GetLocalBounds());
  }
  // Paint the disclosure chevron.
  const int left_pad = GetLayoutManager()
                           ? static_cast<views::BoxLayout*>(GetLayoutManager())
                                 ->inside_border_insets()
                                 .left()
                           : kHPadding;
  const int chevron_y = (height() - kChevronSize) / 2;
  PaintChevron(canvas, left_pad, chevron_y, expanded_);
}

bool ImportedFolderRow::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsOnlyLeftMouseButton() && on_toggle_) {
    on_toggle_.Run(source_id_, item_id_);
    return true;
  }
  return false;
}

void ImportedFolderRow::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  SchedulePaint();
}

void ImportedFolderRow::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  SchedulePaint();
}

BEGIN_METADATA(ImportedFolderRow)
END_METADATA

// ═════════════════════════════════════════════════════════════════════════════
// ImportedSectionHeader
// ═════════════════════════════════════════════════════════════════════════════

ImportedSectionHeader::ImportedSectionHeader(
    bool expanded,
    base::RepeatingCallback<void()> on_toggle)
    : expanded_(expanded), on_toggle_(std::move(on_toggle)) {
  SetPreferredSize(gfx::Size(0, kHeaderHeight));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(0, kHPadding, 0, kHPadding), 6));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Chevron space is painted in OnPaintBackground.
  auto spacer = std::make_unique<views::View>();
  spacer->SetPreferredSize(gfx::Size(kChevronSize + 4, kChevronSize));
  AddChildView(std::move(spacer));

  auto lbl = std::make_unique<views::Label>(u"Imported");
  lbl->SetEnabledColor(kSubtleColor);
  lbl->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL, 11,
                                  gfx::Font::Weight::MEDIUM));
  lbl->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label_ = AddChildView(std::move(lbl));
  layout->SetFlexForView(label_, 1);
}

ImportedSectionHeader::~ImportedSectionHeader() = default;

void ImportedSectionHeader::SetExpanded(bool expanded) {
  expanded_ = expanded;
  SchedulePaint();
}

void ImportedSectionHeader::OnPaintBackground(gfx::Canvas* canvas) {
  if (hovered_) {
    PaintHoverBackground(canvas, GetLocalBounds());
  }
  const int chevron_y = (height() - kChevronSize) / 2;
  PaintChevron(canvas, kHPadding, chevron_y, expanded_);
}

bool ImportedSectionHeader::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsOnlyLeftMouseButton() && on_toggle_) {
    on_toggle_.Run();
    return true;
  }
  return false;
}

void ImportedSectionHeader::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  SchedulePaint();
}

void ImportedSectionHeader::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  SchedulePaint();
}

BEGIN_METADATA(ImportedSectionHeader)
END_METADATA

// ═════════════════════════════════════════════════════════════════════════════
// ImportedSourceHeader
// ═════════════════════════════════════════════════════════════════════════════

ImportedSourceHeader::ImportedSourceHeader(const std::string& source_id,
                                           const std::string& browser,
                                           const std::string& profile_name,
                                           ContextCallback on_context)
    : source_id_(source_id), on_context_(std::move(on_context)) {
  SetPreferredSize(gfx::Size(0, kSourceHeaderHeight));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(0, kHPadding + kIndentPerLevel, 0, kHPadding), 0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Format: "Chrome — Personal", "Edge — Default", "Firefox — default",
  // "Safari — Bookmarks".
  std::string display_browser;
  if (browser == "chrome") {
    display_browser = "Chrome";
  } else if (browser == "edge") {
    display_browser = "Edge";
  } else if (browser == "firefox") {
    display_browser = "Firefox";
  } else if (browser == "safari") {
    display_browser = "Safari";
  } else {
    display_browser = browser;
    if (!display_browser.empty()) {
      display_browser[0] = base::ToUpperASCII(display_browser[0]);
    }
  }
  std::string text = display_browser;
  if (!profile_name.empty()) {
    text += " \xe2\x80\x94 " + profile_name;  // em dash
  }

  auto lbl = std::make_unique<views::Label>(base::UTF8ToUTF16(text));
  lbl->SetEnabledColor(kSourceLabelColor);
  lbl->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL, 10,
                                  gfx::Font::Weight::MEDIUM));
  lbl->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  lbl->SetElideBehavior(gfx::ELIDE_TAIL);
  AddChildView(std::move(lbl));
  layout->SetFlexForView(children().back(), 1);
}

ImportedSourceHeader::~ImportedSourceHeader() = default;

bool ImportedSourceHeader::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsOnlyRightMouseButton() && on_context_) {
    on_context_.Run(source_id_,
                    event.IsOnlyRightMouseButton()
                        ? GetBoundsInScreen().origin() +
                              gfx::Vector2d(event.x(), event.y())
                        : gfx::Point());
    return true;
  }
  return false;
}

BEGIN_METADATA(ImportedSourceHeader)
END_METADATA

// ═════════════════════════════════════════════════════════════════════════════
// AvoraImportedSectionView
// ═════════════════════════════════════════════════════════════════════════════

AvoraImportedSectionView::AvoraImportedSectionView(
    BrowserWindowInterface* browser,
    WindowSpaceState* window_space_state)
    : browser_(browser), window_space_state_(window_space_state) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));

  Profile* profile =
      Profile::FromBrowserContext(browser_->GetProfile());
  store_ = std::make_unique<ImportedLinkStore>(profile->GetPrefs());
  store_->AddObserver(this);

  if (window_space_state_) {
    window_space_state_->AddObserver(this);
  }

  Rebuild();
}

AvoraImportedSectionView::~AvoraImportedSectionView() {
  store_->RemoveObserver(this);
  if (window_space_state_) {
    window_space_state_->RemoveObserver(this);
  }
}

// ── Observer callbacks ──────────────────────────────────────────────────────

void AvoraImportedSectionView::OnImportedLinksChanged() {
  // Auto-expand when imported content appears for the first time.
  if (!section_expanded_ && store_ && window_space_state_) {
    auto sources = store_->GetSourcesForSpace(
        window_space_state_->active_space_id());
    if (!sources.empty()) {
      section_expanded_ = true;
    }
  }
  ScheduleRebuild();
}

void AvoraImportedSectionView::OnWindowActiveSpaceChanged(
    const std::string& space_id) {
  // Space changed — collapse all folders since they belong to the
  // previous Space's data.  The section-level expanded state is
  // preserved across Space switches so the user doesn't have to
  // re-expand after switching back.
  expanded_folders_.clear();
  ScheduleRebuild();
}

// ── Layout ──────────────────────────────────────────────────────────────────

gfx::Size AvoraImportedSectionView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  // Sum heights of all visible children (set by BoxLayout).
  int total_height = 0;
  for (const views::View* child : children()) {
    if (child->GetVisible()) {
      total_height += child->GetPreferredSize(available_size).height();
    }
  }
  return gfx::Size(available_size.width().value_or(0), total_height);
}

// ── Rebuild ─────────────────────────────────────────────────────────────────

void AvoraImportedSectionView::ScheduleRebuild() {
  if (rebuild_pending_) {
    return;
  }
  rebuild_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&AvoraImportedSectionView::Rebuild,
                     weak_factory_.GetWeakPtr()));
}

void AvoraImportedSectionView::Rebuild() {
  rebuild_pending_ = false;
  cancelable_task_tracker_.TryCancelAll();
  RemoveAllChildViews();

  const std::string space_id =
      window_space_state_ ? window_space_state_->active_space_id()
                          : std::string();

  const std::vector<ImportedSource> sources =
      space_id.empty() ? std::vector<ImportedSource>{}
                       : store_->GetSourcesForSpace(space_id);

  if (sources.empty()) {
    // Show a minimal "Import Bookmarks" link row so the user can
    // discover the import feature even before any data exists.
    auto* import_row = AddChildView(std::make_unique<ImportedLinkRow>(
        /*source_id=*/std::string(), /*item_id=*/std::string(),
        /*url=*/std::string(), "Import Bookmarks\u2026", /*depth=*/0,
        base::BindRepeating(
            [](base::WeakPtr<AvoraImportedSectionView> self,
               const std::string&, bool) {
              if (self) self->OnImportClicked();
            },
            weak_factory_.GetWeakPtr()),
        ImportedLinkRow::ContextCallback()));

    // Override the favicon with a "+" indicator.  The row will not
    // attempt favicon loading because the URL is empty.
    (void)import_row;

    InvalidateLayout();
    return;
  }

  // Section header — always visible when there are sources.
  AddChildView(std::make_unique<ImportedSectionHeader>(
      section_expanded_,
      base::BindRepeating(&AvoraImportedSectionView::OnSectionToggle,
                          base::Unretained(this))));

  if (!section_expanded_) {
    InvalidateLayout();
    return;
  }

  // Expanded: show each source with its bookmark tree.
  for (const auto& source : sources) {
    // Source sub-header with context menu.
    AddChildView(std::make_unique<ImportedSourceHeader>(
        source.id, source.browser, source.profile_name,
        base::BindRepeating(
            &AvoraImportedSectionView::OnSourceContextMenu,
            base::Unretained(this))));

    // Root-level items of this source.
    BuildSubtree(source, /*parent_id=*/std::string(), /*depth=*/1);
  }

  // "Import More" row at the bottom of the expanded section.
  AddChildView(std::make_unique<ImportedLinkRow>(
      /*source_id=*/std::string(), /*item_id=*/std::string(),
      /*url=*/std::string(), "Import Bookmarks\u2026", /*depth=*/0,
      base::BindRepeating(
          [](base::WeakPtr<AvoraImportedSectionView> self,
             const std::string&, bool) {
            if (self) self->OnImportClicked();
          },
          weak_factory_.GetWeakPtr()),
      ImportedLinkRow::ContextCallback()));

  InvalidateLayout();
}

void AvoraImportedSectionView::BuildSubtree(const ImportedSource& source,
                                             const std::string& parent_id,
                                             int depth) {
  auto children = store_->GetChildren(source.id, parent_id);
  for (const auto& item : children) {
    if (item.type == ImportedItemType::kFolder) {
      const bool expanded = IsFolderExpanded(source.id, item.id);
      AddChildView(std::make_unique<ImportedFolderRow>(
          source.id, item.id, item.title, expanded, depth,
          base::BindRepeating(&AvoraImportedSectionView::OnFolderToggle,
                              base::Unretained(this))));
      if (expanded) {
        BuildSubtree(source, item.id, depth + 1);
      }
    } else {
      auto* row = AddChildView(std::make_unique<ImportedLinkRow>(
          source.id, item.id, item.url, item.title, depth,
          base::BindRepeating(&AvoraImportedSectionView::OnLinkClicked,
                              base::Unretained(this)),
          base::BindRepeating(&AvoraImportedSectionView::OnLinkContextMenu,
                              base::Unretained(this))));
      LoadFavicon(row);
    }
  }
}

// ── Interaction handlers ────────────────────────────────────────────────────

void AvoraImportedSectionView::OnSectionToggle() {
  section_expanded_ = !section_expanded_;
  Rebuild();
}

void AvoraImportedSectionView::OnImportClicked() {
  if (browser_) {
    AvoraImportDialogView::Show(
        browser_, window_space_state_,
        base::BindOnce(&AvoraImportedSectionView::RevealSource,
                       weak_factory_.GetWeakPtr()));
  }
}

void AvoraImportedSectionView::OnFolderToggle(const std::string& source_id,
                                               const std::string& item_id) {
  const std::string key = FolderKey(source_id, item_id);
  if (expanded_folders_.contains(key)) {
    expanded_folders_.erase(key);
  } else {
    expanded_folders_.insert(key);
  }
  Rebuild();
}

bool AvoraImportedSectionView::IsFolderExpanded(
    const std::string& source_id,
    const std::string& item_id) const {
  return expanded_folders_.contains(FolderKey(source_id, item_id));
}

// ── Navigation ──────────────────────────────────────────────────────────────

void AvoraImportedSectionView::OnLinkClicked(const std::string& url,
                                              bool background) {
  if (!browser_ || url.empty()) {
    return;
  }

  const GURL gurl(url);
  if (!gurl.is_valid()) {
    return;
  }

  NavigateParams params(browser_, gurl, ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = background ? WindowOpenDisposition::NEW_BACKGROUND_TAB
                                  : WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

// ── Context menu ────────────────────────────────────────────────────────────

void AvoraImportedSectionView::OnLinkContextMenu(
    const std::string& source_id,
    const std::string& item_id,
    const std::string& url,
    const std::string& title,
    const gfx::Point& screen_point) {
  // Stash context for command execution.
  context_source_id_ = source_id;
  context_item_id_ = item_id;
  context_url_ = url;
  context_title_ = title;

  context_menu_delegate_ = std::make_unique<ImportedLinkMenuDelegate>(
      base::BindRepeating(&AvoraImportedSectionView::ExecuteContextMenuCommand,
                          base::Unretained(this)));

  context_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(context_menu_delegate_.get());
  context_menu_model_->AddItem(kMenuOpenId, u"Open");
  context_menu_model_->AddItem(kMenuOpenBackgroundId,
                               u"Open in Background Tab");
  context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  context_menu_model_->AddItem(kMenuCopyLinkId, u"Copy Link");
  context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  context_menu_model_->AddItem(kMenuAddToFavoritesId, u"Add to Favorites");
  context_menu_model_->AddItem(kMenuRemoveFromImportedId,
                               u"Remove from Imported");

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(GetWidget(), nullptr,
                                  gfx::Rect(screen_point, gfx::Size()),
                                  views::MenuAnchorPosition::kTopLeft,
                                  ui::mojom::MenuSourceType::kMouse);
}

void AvoraImportedSectionView::ExecuteContextMenuCommand(int command_id) {
  switch (command_id) {
    case kMenuOpenId:
      OnLinkClicked(context_url_, /*background=*/false);
      break;

    case kMenuOpenBackgroundId:
      OnLinkClicked(context_url_, /*background=*/true);
      break;

    case kMenuCopyLinkId: {
      ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
      writer.WriteText(base::UTF8ToUTF16(context_url_));
      break;
    }

    case kMenuAddToFavoritesId: {
      if (!browser_) {
        break;
      }
      Profile* profile =
          Profile::FromBrowserContext(browser_->GetProfile());
      FavoritesManager favorites_manager(profile->GetPrefs());
      if (window_space_state_) {
        favorites_manager.SetWindowActiveSpaceId(
            window_space_state_->active_space_id());
      }
      favorites_manager.AddFavorite(context_url_, context_title_);
      break;
    }

    case kMenuRemoveFromImportedId:
      store_->RemoveItem(context_source_id_, context_item_id_);
      break;
  }
}

// ── Favicon loading ─────────────────────────────────────────────────────────

void AvoraImportedSectionView::LoadFavicon(ImportedLinkRow* row) {
  Profile* profile =
      Profile::FromBrowserContext(browser_->GetProfile());
  favicon::FaviconService* favicon_service =
      FaviconServiceFactory::GetForProfile(
          profile, ServiceAccessType::EXPLICIT_ACCESS);
  if (!favicon_service) {
    return;
  }

  const std::string url = row->url();
  base::WeakPtr<AvoraImportedSectionView> weak_this =
      weak_factory_.GetWeakPtr();
  const std::string source_id = row->source_id();
  const std::string item_id = row->item_id();

  favicon_service->GetRawFaviconForPageURL(
      GURL(url), {favicon_base::IconType::kFavicon}, kFaviconSize,
      /*fallback_to_host=*/true,
      base::BindOnce(
          [](base::WeakPtr<AvoraImportedSectionView> view,
             const std::string& source_id, const std::string& item_id,
             const favicon_base::FaviconRawBitmapResult& result) {
            if (!view || !result.is_valid()) {
              return;
            }
            gfx::Image image =
                gfx::Image::CreateFrom1xPNGBytes(result.bitmap_data);
            if (image.IsEmpty()) {
              return;
            }
            // Find the matching row (it may have been rebuilt).
            for (views::View* child : view->children()) {
              auto* link_row = views::AsViewClass<ImportedLinkRow>(child);
              if (link_row && link_row->source_id() == source_id &&
                  link_row->item_id() == item_id) {
                link_row->SetFavicon(*image.ToImageSkia());
                break;
              }
            }
          },
          weak_this, source_id, item_id),
      &cancelable_task_tracker_);
}

void AvoraImportedSectionView::ExpandSection() {
  if (!section_expanded_) {
    section_expanded_ = true;
    ScheduleRebuild();
  }
}

void AvoraImportedSectionView::RevealSource(const std::string& source_id) {
  // Expand the section.
  section_expanded_ = true;

  // Rebuild synchronously to create the source header views.
  Rebuild();

  // Find the source header for this source_id and scroll it into view.
  for (views::View* child : children()) {
    auto* header = views::AsViewClass<ImportedSourceHeader>(child);
    if (header && header->source_id() == source_id) {
      header->ScrollViewToVisible();
      return;
    }
  }
}

// ─── Source context menu ─────────────────────────────────────────────────────

namespace {
constexpr int kSourceMenuUpdateId = 200;
constexpr int kSourceMenuRemoveId = 201;
}  // namespace

void AvoraImportedSectionView::OnSourceContextMenu(
    const std::string& source_id,
    const gfx::Point& screen_point) {
  source_context_source_id_ = source_id;

  // Build the source-level context menu.
  class SourceMenuDelegate : public ui::SimpleMenuModel::Delegate {
   public:
    explicit SourceMenuDelegate(AvoraImportedSectionView* owner)
        : owner_(owner) {}
    void ExecuteCommand(int command_id, int event_flags) override {
      owner_->ExecuteSourceContextMenuCommand(command_id);
    }
    bool IsCommandIdEnabled(int command_id) const override { return true; }

   private:
    raw_ptr<AvoraImportedSectionView> owner_;
  };

  source_context_menu_delegate_ =
      std::make_unique<SourceMenuDelegate>(this);
  source_context_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(
          source_context_menu_delegate_.get());

  source_context_menu_model_->AddItem(
      kSourceMenuUpdateId, u"Update Import");
  source_context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  source_context_menu_model_->AddItem(
      kSourceMenuRemoveId, u"Remove Imported Bookmarks");

  source_context_menu_runner_ = std::make_unique<views::MenuRunner>(
      source_context_menu_model_.get(),
      views::MenuRunner::IS_NESTED | views::MenuRunner::CONTEXT_MENU);
  source_context_menu_runner_->RunMenuAt(
      GetWidget(), nullptr, gfx::Rect(screen_point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft,
      ui::mojom::MenuSourceType::kMouse);
}

void AvoraImportedSectionView::ExecuteSourceContextMenuCommand(
    int command_id) {
  if (command_id == kSourceMenuUpdateId) {
    // Open the import dialog pre-selected to this source.
    if (browser_) {
      AvoraImportDialogView::Show(
          browser_, window_space_state_,
          base::BindOnce(&AvoraImportedSectionView::RevealSource,
                         weak_factory_.GetWeakPtr()),
          source_context_source_id_);
    }
  } else if (command_id == kSourceMenuRemoveId) {
    if (store_ && !source_context_source_id_.empty()) {
      store_->RemoveSource(source_context_source_id_);
    }
  }
}

BEGIN_METADATA(AvoraImportedSectionView)
END_METADATA

}  // namespace avora
