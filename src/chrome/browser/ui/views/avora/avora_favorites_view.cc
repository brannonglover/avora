// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_favorites_view.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "base/pickle.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/avora/avora_imported_link_store.h"
#include "chrome/browser/ui/views/avora/avora_imported_section_view.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_callback.h"
#include "components/favicon_base/favicon_types.h"
#include "cc/paint/paint_flags.h"
#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkColor.h"
#include "chrome/browser/avora/avora_tab_space.h"
#include "chrome/browser/ui/views/avora/avora_favorite_tab_marker.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/animating_layout_manager.h"
#include "ui/views/layout/layout_manager_base.h"
#include "ui/views/layout/proposed_layout.h"
#include "ui/views/view_utils.h"
#include "ui/base/clipboard/clipboard_url_info.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/compositor/layer_tree_owner.h"
#include "url/gurl.h"

namespace avora {

namespace {

constexpr int kContextMenuRemoveId = 1;

// Grid layout constants (shared between layout and hit-testing).
constexpr int kHMargin = 8;
constexpr int kFixedGap = 10;

// Grid layout manager that places children in rows of up to kMaxPerRow icons.
// Supports an insertion gap: when gap_index >= 0 an empty cell-sized space is
// reserved at that position and all subsequent children shift right.
class FavoritesGridLayout : public views::LayoutManagerBase {
 public:
  FavoritesGridLayout() = default;
  ~FavoritesGridLayout() override = default;

  void SetGapIndex(int index) {
    if (gap_index_ == index) {
      return;
    }
    gap_index_ = index;
    InvalidateHost(true);
  }
  int gap_index() const { return gap_index_; }

 protected:
  views::ProposedLayout CalculateProposedLayout(
      const views::SizeBounds& size_bounds) const override {
    views::ProposedLayout layouts;

    const int min_btn_size = AvoraFavoritesView::kButtonSize;
    const int btn_height = AvoraFavoritesView::kButtonHeight;
    const int v_padding = AvoraFavoritesView::kVerticalPadding;

    int host_width = size_bounds.width().value_or(0);
    int usable_width = host_width - kHMargin * 2;

    int max_per_row = AvoraFavoritesView::kMaxPerRow;
    if (usable_width > 0) {
      max_per_row = std::max(
          1, (usable_width + kFixedGap) / (min_btn_size + kFixedGap));
      max_per_row = std::min(max_per_row, AvoraFavoritesView::kMaxPerRow);
    }

    // Count visible children.
    int visible_count = 0;
    for (views::View* child : host_view()->children()) {
      if (child->GetVisible()) {
        visible_count++;
      }
    }

    // Total slots = visible children + 1 if there is an active gap.
    const bool has_gap = gap_index_ >= 0 && gap_index_ <= visible_count;
    const int total_slots = visible_count + (has_gap ? 1 : 0);

    // Place each visible child, skipping over the gap slot.
    // Each cell expands to fill the row with a fixed kFixedGap (10px)
    // between adjacent cells. The background fills the full cell width.
    int visible_idx = 0;
    for (views::View* child : host_view()->children()) {
      if (!child->GetVisible()) {
        layouts.child_layouts.emplace_back(child, false, gfx::Rect());
        continue;
      }

      // The effective slot index: shift by one past the gap.
      int slot = visible_idx;
      if (has_gap && visible_idx >= gap_index_) {
        slot = visible_idx + 1;
      }

      int row = slot / max_per_row;
      int col = slot % max_per_row;

      // Fixed 4-column grid: every cell is the same width regardless of
      // how many items are in the row.
      int cell_width = min_btn_size;
      if (max_per_row > 0 && usable_width > 0) {
        cell_width = std::max(min_btn_size,
            (usable_width - (max_per_row - 1) * kFixedGap) / max_per_row);
      }

      int x = kHMargin + col * (cell_width + kFixedGap);
      int y = v_padding + row * (btn_height + v_padding);
      gfx::Rect bounds(x, y, cell_width, btn_height);
      layouts.child_layouts.emplace_back(child, true, bounds);
      visible_idx++;
    }

    // Calculate total height.
    int total_rows = total_slots > 0
                         ? ((total_slots - 1) / max_per_row) + 1
                         : 0;
    int total_height = 0;
    if (total_rows > 0) {
      total_height =
          v_padding + total_rows * btn_height +
          (total_rows - 1) * v_padding + v_padding;
    }

    layouts.host_size =
        gfx::Size(host_width, total_slots == 0 ? 0 : total_height);
    return layouts;
  }

 private:
  int gap_index_ = -1;
};

// Simple delegate for the remove-from-favorites context menu.
class FavoriteContextMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  explicit FavoriteContextMenuDelegate(base::OnceClosure on_remove)
      : on_remove_(std::move(on_remove)) {}

  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == kContextMenuRemoveId && on_remove_) {
      std::move(on_remove_).Run();
    }
  }

 private:
  base::OnceClosure on_remove_;
};

}  // namespace

// ---------------------------------------------------------------------------
// FavoriteButton
// ---------------------------------------------------------------------------

FavoriteButton::FavoriteButton(
    const std::string& url,
    const std::string& title,
    base::RepeatingCallback<void(const std::string&)> on_click,
    base::RepeatingCallback<void(const std::string&, const gfx::Point&)>
        on_context)
    : url_(url), title_(title), on_context_(std::move(on_context)) {
  SetImageHorizontalAlignment(ALIGN_CENTER);
  SetImageVerticalAlignment(ALIGN_MIDDLE);
  SetTooltipText(base::UTF8ToUTF16(title.empty() ? url : title));
  SetCallback(base::BindRepeating(std::move(on_click), url));
  SetPreferredSize(gfx::Size(AvoraFavoritesView::kButtonSize,
                              AvoraFavoritesView::kButtonHeight));
  SetBorder(views::CreateEmptyBorder(
      gfx::Insets::VH(AvoraFavoritesView::kButtonVInset,
                       AvoraFavoritesView::kButtonHInset)));
  set_context_menu_controller(this);
}

void FavoriteButton::SetActive(bool active) {
  if (is_active_ == active) {
    return;
  }
  is_active_ = active;
  SchedulePaint();
}

void FavoriteButton::OnPaintBackground(gfx::Canvas* canvas) {
  constexpr SkColor kBgNormal = SkColorSetARGB(0x1A, 0xFF, 0xFF, 0xFF);
  constexpr SkColor kBgActive = SkColorSetARGB(0x40, 0x6E, 0xA8, 0xFF);
  constexpr SkColor kBorderActive = SkColorSetARGB(0x90, 0x6E, 0xA8, 0xFF);

  const float r = static_cast<float>(AvoraFavoritesView::kButtonRadius);

  cc::PaintFlags fill_flags;
  fill_flags.setAntiAlias(true);
  fill_flags.setColor(is_active_ ? kBgActive : kBgNormal);
  fill_flags.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawRoundRect(GetLocalBounds(), r, fill_flags);

  if (is_active_) {
    cc::PaintFlags border_flags;
    border_flags.setAntiAlias(true);
    border_flags.setColor(kBorderActive);
    border_flags.setStyle(cc::PaintFlags::kStroke_Style);
    border_flags.setStrokeWidth(1.5f);
    gfx::RectF inset(GetLocalBounds());
    inset.Inset(0.75f);
    canvas->DrawRoundRect(gfx::ToEnclosingRect(inset), r, border_flags);
  }
}

void FavoriteButton::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (on_context_) {
    on_context_.Run(url_, point);
  }
}

FavoriteButton::~FavoriteButton() = default;

void FavoriteButton::SetFavicon(const gfx::ImageSkia& icon) {
  if (icon.isNull()) {
    return;
  }
  gfx::ImageSkia resized = gfx::ImageSkiaOperations::CreateResizedImage(
      icon, skia::ImageOperations::RESIZE_BEST,
      gfx::Size(AvoraFavoritesView::kFaviconSize,
                AvoraFavoritesView::kFaviconSize));
  SetImageModel(views::Button::STATE_NORMAL,
                ui::ImageModel::FromImageSkia(resized));
}

BEGIN_METADATA(FavoriteButton)
END_METADATA

// ---------------------------------------------------------------------------
// AvoraFavoritesView
// ---------------------------------------------------------------------------

AvoraFavoritesView::AvoraFavoritesView(BrowserWindowInterface* browser,
                                       avora::WindowSpaceState* window_space_state)
    : browser_(browser), window_space_state_(window_space_state) {
  // Use the grid layout directly (no animation wrapper).
  auto grid = std::make_unique<FavoritesGridLayout>();
  grid_layout_ = grid.get();
  SetLayoutManager(std::move(grid));

  Profile* profile = browser_->GetProfile();
  favorites_manager_ =
      std::make_unique<FavoritesManager>(profile->GetPrefs());
  favorites_manager_->AddObserver(this);

  // Seed the manager with the window-local active Space.
  if (window_space_state_) {
    favorites_manager_->SetWindowActiveSpaceId(
        window_space_state_->active_space_id());
    window_space_state_->AddObserver(this);
  }

  // Observe tab activation changes to highlight the active favorite.
  TabStripModel* tab_strip = browser_->GetTabStripModel();
  if (tab_strip) {
    tab_strip->AddObserver(this);
  }

  RebuildGrid();
}

AvoraFavoritesView::~AvoraFavoritesView() {
  if (window_space_state_) {
    window_space_state_->RemoveObserver(this);
  }
  if (favorites_manager_) {
    favorites_manager_->RemoveObserver(this);
  }
  if (browser_) {
    TabStripModel* tab_strip = browser_->GetTabStripModel();
    if (tab_strip) {
      tab_strip->RemoveObserver(this);
    }
  }
}

void AvoraFavoritesView::OnFavoritesChanged() {
  RebuildGrid();
}

void AvoraFavoritesView::OnWindowActiveSpaceChanged(
    const std::string& space_id) {
  if (favorites_manager_) {
    favorites_manager_->SetWindowActiveSpaceId(space_id);
  }
}

void AvoraFavoritesView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // Update highlight when the user switches tabs.
  if (selection.active_tab_changed()) {
    UpdateActiveState();
  }
}

void AvoraFavoritesView::OnTabChangedAt(tabs::TabInterface* tab,
                                         TabChangeType change_type) {
  // Update highlight when the active tab's URL changes (e.g. after
  // navigating it to a favorite URL).
  UpdateActiveState();
}

void AvoraFavoritesView::UpdateActiveState() {
  if (!browser_) {
    return;
  }
  TabStripModel* tab_strip = browser_->GetTabStripModel();
  GURL active_url;
  if (tab_strip) {
    content::WebContents* active = tab_strip->GetActiveWebContents();
    if (active) {
      active_url = active->GetLastCommittedURL();
      if (active_url.is_empty()) {
        active_url = active->GetVisibleURL();
      }
    }
  }

  for (views::View* child : children()) {
    auto* btn = static_cast<FavoriteButton*>(child);
    if (btn) {
      GURL fav_url(btn->url());
      bool match = (active_url.spec() == btn->url()) ||
                   (fav_url.has_host() && active_url.has_host() &&
                    fav_url.host() == active_url.host());
      btn->SetActive(match);
    }
  }
}

bool AvoraFavoritesView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  *formats = ui::OSExchangeData::URL;
  format_types->insert(GetImportedLinkClipboardFormatType());
  return true;
}

bool AvoraFavoritesView::CanDrop(const ui::OSExchangeData& data) {
  return data.HasURL(ui::FilenameToURLPolicy::DO_NOT_CONVERT_FILENAMES) ||
         data.HasCustomFormat(GetImportedLinkClipboardFormatType());
}

int AvoraFavoritesView::OnDragUpdated(const ui::DropTargetEvent& event) {
  return ui::DragDropTypes::DRAG_MOVE;
}

views::View::DropCallback AvoraFavoritesView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  return base::BindOnce(
      [](base::WeakPtr<AvoraFavoritesView> view,
         const ui::DropTargetEvent& event,
         ui::mojom::DragOperation& output_drag_op,
         std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
        if (!view) {
          output_drag_op = ui::mojom::DragOperation::kNone;
          return;
        }

        // If the drag payload contains an Avora imported-link identity,
        // treat it as authoritative.  Either the item resolves to a valid
        // link and we add a Favorite, or we reject the drop entirely.
        // We never fall through to the generic URL path when the custom
        // payload is present — the URL representation may be stale.
        const bool has_imported_link_format =
            event.data().HasCustomFormat(
                GetImportedLinkClipboardFormatType());
        if (has_imported_link_format) {
          std::optional<base::Pickle> pickle =
              event.data().GetPickledData(
                  GetImportedLinkClipboardFormatType());
          if (pickle.has_value()) {
            base::PickleIterator iter(pickle.value());
            std::string source_id;
            std::string item_id;
            if (iter.ReadString(&source_id) && iter.ReadString(&item_id)) {
              Profile* profile = Profile::FromBrowserContext(
                  view->browser_->GetProfile());
              ImportedLinkStore store(profile->GetPrefs());
              const ImportedItem* item =
                  store.GetItemById(source_id, item_id);
              if (item && item->type == ImportedItemType::kLink) {
                view->GetFavoritesManager()->AddFavorite(
                    item->url,
                    item->title.empty() ? item->url : item->title);
                output_drag_op = ui::mojom::DragOperation::kCopy;
                return;
              }
            }
          }
          // Custom payload present but malformed, stale, or a folder.
          output_drag_op = ui::mojom::DragOperation::kNone;
          return;
        }

        // Generic URL drop (external apps, other browsers, etc.).
        auto urls = event.data().GetURLs(
            ui::FilenameToURLPolicy::DO_NOT_CONVERT_FILENAMES);
        if (!urls.empty() && urls[0].url.is_valid()) {
          const GURL& dropped_url = urls[0].url;
          view->GetFavoritesManager()->AddFavorite(
              dropped_url.spec(),
              base::UTF16ToUTF8(urls[0].title));
          output_drag_op = ui::mojom::DragOperation::kMove;
        } else {
          output_drag_op = ui::mojom::DragOperation::kNone;
        }
      },
      weak_factory_.GetWeakPtr());
}

void AvoraFavoritesView::OnPaintBackground(gfx::Canvas* canvas) {
  if (!is_drag_active_) {
    return;
  }

  gfx::Rect bounds = GetLocalBounds();
  if (bounds.IsEmpty()) {
    return;
  }

  constexpr SkColor kInsertionLineColor = SkColorSetARGB(0xFF, 0x6E, 0xA8, 0xFF);
  constexpr int kLineThickness = 2;

  if (is_drop_highlighted_ && insertion_gap_index_ >= 0) {
    // Draw a blue insertion line at the gap position.
    int usable_width = bounds.width() - kHMargin * 2;
    int max_per_row = kMaxPerRow;
    if (usable_width > 0) {
      max_per_row = std::max(
          1, (usable_width + kFixedGap) / (kButtonSize + kFixedGap));
      max_per_row = std::min(max_per_row, kMaxPerRow);
    }
    int cell_width = kButtonSize;
    if (max_per_row > 0 && usable_width > 0) {
      cell_width = std::max(kButtonSize,
          (usable_width - (max_per_row - 1) * kFixedGap) / max_per_row);
    }
    int cell_stride = cell_width + kFixedGap;
    int row_stride = kButtonHeight + kVerticalPadding;
    int gap_row = insertion_gap_index_ / max_per_row;
    int gap_col = insertion_gap_index_ % max_per_row;

    // Draw a vertical line at the gap column edge.
    int line_x = kHMargin + gap_col * cell_stride - kFixedGap / 2;
    int line_y = kVerticalPadding + gap_row * row_stride;
    int line_height = kButtonHeight;

    // If inserting at the end of a row or at the very end of the grid,
    // draw the line after the last icon in that row.
    if (gap_col == 0 && insertion_gap_index_ > 0) {
      // Wrapping to a new row: draw a horizontal line between rows.
      int prev_row = gap_row - 1;
      int hl_y = kVerticalPadding + prev_row * row_stride + kButtonHeight +
                 kVerticalPadding / 2;
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(kInsertionLineColor);
      flags.setStyle(cc::PaintFlags::kFill_Style);
      canvas->DrawRect(
          gfx::RectF(kHMargin, hl_y, usable_width, kLineThickness), flags);
    } else {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(kInsertionLineColor);
      flags.setStyle(cc::PaintFlags::kFill_Style);
      canvas->DrawRect(
          gfx::RectF(line_x, line_y, kLineThickness, line_height), flags);
    }
  } else if (is_drop_highlighted_ || favorites_manager_->GetFavorites().empty()) {
    // When highlighted but no gap yet, or empty placeholder: draw a
    // horizontal line at the top to hint "drop here".
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(kInsertionLineColor);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    int line_y = kVerticalPadding;
    canvas->DrawRect(
        gfx::RectF(kHMargin, line_y,
                    bounds.width() - kHMargin * 2, kLineThickness),
        flags);
  }
}

gfx::Size AvoraFavoritesView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  // During an active drag with no favorites, provide a minimum height so the
  // view is visible as a drop zone.
  gfx::Size size = views::View::CalculatePreferredSize(available_size);
  if (is_drag_active_ && favorites_manager_->GetFavorites().empty()) {
    size.set_height(std::max(size.height(), kDropPlaceholderHeight));
  }
  return size;
}

void AvoraFavoritesView::RebuildGrid() {
  RemoveAllChildViews();

  auto favorites = favorites_manager_->GetFavorites();
  for (const auto& entry : favorites) {
    auto* button = AddChildView(std::make_unique<FavoriteButton>(
        entry.url, entry.title,
        base::BindRepeating(&AvoraFavoritesView::OnFavoriteClicked,
                            base::Unretained(this)),
        base::BindRepeating(&AvoraFavoritesView::OnFavoriteContextMenu,
                            base::Unretained(this))));
    LoadFavicon(button);
  }

  // Visible when favorites exist, OR when a drag is active (shows placeholder).
  SetVisible(!favorites.empty() || is_drag_active_);
  UpdateActiveState();
  InvalidateLayout();
  if (parent()) {
    parent()->InvalidateLayout();
  }
}

FavoritesManager* AvoraFavoritesView::GetFavoritesManager() const {
  return favorites_manager_.get();
}

void AvoraFavoritesView::SetDragActive(bool active) {
  if (is_drag_active_ == active) {
    return;
  }
  is_drag_active_ = active;
  if (!active) {
    is_drop_highlighted_ = false;
    ClearInsertionGap();
  }
  // Show the view during a drag even if empty, so the user has a drop target.
  auto favorites = favorites_manager_->GetFavorites();
  SetVisible(!favorites.empty() || is_drag_active_);
  InvalidateLayout();
  if (parent()) {
    parent()->InvalidateLayout();
  }
  SchedulePaint();
}

void AvoraFavoritesView::SetDropHighlighted(bool highlighted) {
  if (is_drop_highlighted_ == highlighted) {
    return;
  }
  is_drop_highlighted_ = highlighted;
  if (!highlighted) {
    ClearInsertionGap();
  }
  SchedulePaint();
}

void AvoraFavoritesView::UpdateInsertionGap(
    const gfx::Point& screen_point) {
  gfx::Point local = screen_point;
  ConvertPointFromScreen(this, &local);

  const int btn_height = kButtonHeight;
  const int v_padding = kVerticalPadding;

  int width = this->width();
  int usable_width = width - kHMargin * 2;

  int max_per_row = kMaxPerRow;
  if (usable_width > 0) {
    max_per_row = std::max(
        1, (usable_width + kFixedGap) / (kButtonSize + kFixedGap));
    max_per_row = std::min(max_per_row, kMaxPerRow);
  }

  int cell_size = kButtonSize;
  if (max_per_row > 0 && usable_width > 0) {
    cell_size = std::max(kButtonSize,
        (usable_width - (max_per_row - 1) * kFixedGap) / max_per_row);
  }
  int cell_stride = cell_size + kFixedGap;
  int row_stride = btn_height + v_padding;

  // Determine the row and column from the local cursor position.
  int row = std::max(0, (local.y() - v_padding)) / row_stride;
  int col = std::max(0, (local.x() - kHMargin + cell_stride / 2)) /
            cell_stride;
  col = std::clamp(col, 0, max_per_row);

  int num_favorites =
      static_cast<int>(favorites_manager_->GetFavorites().size());
  int index = row * max_per_row + col;
  index = std::clamp(index, 0, num_favorites);

  if (insertion_gap_index_ != index) {
    insertion_gap_index_ = index;
    if (grid_layout_) {
      static_cast<FavoritesGridLayout*>(grid_layout_.get())->SetGapIndex(index);
    }
  }
}

void AvoraFavoritesView::ClearInsertionGap() {
  if (insertion_gap_index_ < 0) {
    return;
  }
  insertion_gap_index_ = -1;
  if (grid_layout_) {
    static_cast<FavoritesGridLayout*>(grid_layout_.get())->SetGapIndex(-1);
  }
}

void AvoraFavoritesView::EnsureFavoriteTabsExist() {
  if (!browser_) {
    return;
  }
  TabStripModel* tab_strip = browser_->GetTabStripModel();
  if (!tab_strip) {
    return;
  }

  auto favorites = favorites_manager_->GetFavorites();
  for (const auto& entry : favorites) {
    GURL fav_url(entry.url);
    if (!fav_url.is_valid() || !fav_url.has_host()) {
      continue;
    }

    // Check if a tab for this favorite already exists.
    bool found = false;
    for (int i = 0; i < tab_strip->count(); ++i) {
      content::WebContents* contents = tab_strip->GetWebContentsAt(i);
      if (!contents) {
        continue;
      }
      const GURL& committed = contents->GetLastCommittedURL();
      const GURL& visible = contents->GetVisibleURL();
      if (committed.spec() == entry.url || visible.spec() == entry.url ||
          committed.host() == fav_url.host() ||
          visible.host() == fav_url.host()) {
        found = true;
        break;
      }
    }

    if (!found) {
      // Create a background tab for this favorite so it exists when clicked.
      NavigateParams params(browser_, fav_url,
                            ui::PAGE_TRANSITION_AUTO_BOOKMARK);
      params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
      Navigate(&params);
    }
  }
}

void AvoraFavoritesView::OnFavoriteClicked(const std::string& url) {
  if (!browser_) {
    return;
  }

  TabStripModel* tab_strip = browser_->GetTabStripModel();
  if (!tab_strip) {
    return;
  }

  if (!favorites_manager_) {
    return;
  }

  // Resolve the click to a Favorite identity.  Everything below keys off that
  // id rather than comparing URLs, so a tab that has navigated away from the
  // Favorite's target is still recognised as belonging to it.
  const std::string favorite_id =
      favorites_manager_->GetFavoriteIdForUrl(url);
  if (favorite_id.empty()) {
    return;
  }

  // Always navigate to the Favorite's persisted target, not to wherever its
  // tab happens to be. Browsing inside the tab never rewrites the Favorite.
  const GURL fav_url(favorites_manager_->GetFavoriteUrlById(favorite_id));
  if (!fav_url.is_valid()) {
    return;
  }

  const std::string active_space = favorites_manager_->GetActiveSpaceId();

  // Look only within the active Space. Tabs from other Spaces are still in the
  // model but hidden, and activating one would jump to a page with no visible
  // tab in the sidebar.
  for (int i = 0; i < tab_strip->count(); ++i) {
    content::WebContents* contents = tab_strip->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    if (GetFavoriteIdForTab(contents) != favorite_id) {
      continue;
    }
    const std::string tab_space = GetTabSpaceId(contents);
    if (!tab_space.empty() && !active_space.empty() &&
        tab_space != active_space) {
      continue;
    }

    // Activate only.  The Favorite still owns its saved URL, but its tab is
    // free to be somewhere deeper in the site, and yanking it back to the
    // destination on every click would throw away the page the user was on.
    tab_strip->ActivateTabAt(i);
    return;
  }

  // No tab for this Favorite in this Space yet, so open one and claim it.
  NavigateParams params(browser_, fav_url, ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);

  if (params.navigated_or_inserted_contents) {
    // Mark before the first layout pass so the tab is filtered out of the
    // daily list immediately rather than flashing there for a frame.  The tab
    // is freshly created here, so it was never pinned by the user.
    MarkFavoriteTab(params.navigated_or_inserted_contents, favorite_id,
                    /*was_pinned=*/false);
    if (!active_space.empty()) {
      SetTabSpaceId(params.navigated_or_inserted_contents, active_space);
    }
  }
}

void AvoraFavoritesView::OnFavoriteContextMenu(
    const std::string& url,
    const gfx::Point& screen_point) {
  context_menu_delegate_ = std::make_unique<FavoriteContextMenuDelegate>(
      base::BindOnce(&FavoritesManager::RemoveFavorite,
                     base::Unretained(favorites_manager_.get()), url));

  context_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(context_menu_delegate_.get());
  context_menu_model_->AddItem(kContextMenuRemoveId,
                               u"Remove from Favorites");

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(GetWidget(), nullptr,
                                  gfx::Rect(screen_point, gfx::Size()),
                                  views::MenuAnchorPosition::kTopLeft,
                                  ui::mojom::MenuSourceType::kMouse);
}

void AvoraFavoritesView::LoadFavicon(FavoriteButton* button) {
  if (!browser_) {
    return;
  }
  favicon::FaviconService* favicon_service =
      FaviconServiceFactory::GetForProfile(
          browser_->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
  if (!favicon_service) {
    return;
  }

  favicon_service->GetRawFaviconForPageURL(
      GURL(button->url()),
      {favicon_base::IconType::kFavicon},
      kFaviconSize,
      /*fallback_to_host=*/true,
      base::BindOnce(
          [](base::WeakPtr<AvoraFavoritesView> view, const std::string& url,
             const favicon_base::FaviconRawBitmapResult& result) {
            if (!view || !result.is_valid()) {
              return;
            }
            gfx::ImageSkia icon =
                gfx::Image::CreateFrom1xPNGBytes(result.bitmap_data)
                    .AsImageSkia();
            if (icon.isNull()) {
              return;
            }
            if (icon.width() != kFaviconSize ||
                icon.height() != kFaviconSize) {
              icon = gfx::ImageSkiaOperations::CreateResizedImage(
                  icon, skia::ImageOperations::RESIZE_BEST,
                  gfx::Size(kFaviconSize, kFaviconSize));
            }
            for (views::View* child : view->children()) {
              auto* btn = static_cast<FavoriteButton*>(child);
              if (btn && btn->url() == url) {
                btn->SetFavicon(icon);
                break;
              }
            }
          },
          weak_factory_.GetWeakPtr(), button->url()),
      &cancelable_task_tracker_);
}

BEGIN_METADATA(AvoraFavoritesView)
END_METADATA

}  // namespace avora
