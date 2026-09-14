// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_FAVORITES_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_FAVORITES_VIEW_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "chrome/browser/avora/avora_favorites.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/layout_manager_base.h"
#include "ui/views/view.h"

class BrowserWindowInterface;

namespace gfx {
class Canvas;
class ImageSkia;
}

namespace avora {

// A single favorite item: a 32×32 favicon button in the grid.
class FavoriteButton : public views::ImageButton,
                       public views::ContextMenuController {
  METADATA_HEADER(FavoriteButton, views::ImageButton)

 public:
  FavoriteButton(const std::string& url,
                 const std::string& title,
                 base::RepeatingCallback<void(const std::string&)> on_click,
                 base::RepeatingCallback<void(const std::string&,
                                              const gfx::Point&)> on_context);
  ~FavoriteButton() override;

  const std::string& url() const { return url_; }
  const std::string& title() const { return title_; }

  void SetFavicon(const gfx::ImageSkia& icon);

  // Whether this button's tab is the currently active tab.
  void SetActive(bool active);
  bool is_active() const { return is_active_; }

  // views::View:
  void OnPaintBackground(gfx::Canvas* canvas) override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

 private:
  std::string url_;
  std::string title_;
  bool is_active_ = false;
  base::RepeatingCallback<void(const std::string&, const gfx::Point&)>
      on_context_;
};

// Grid view displaying all favorite sites as 32×32 favicon buttons.
// Sits above the pinned tab section in the vertical tab strip.
// Layout: up to 4 icons per row, evenly spaced across the available width.
// Accepts tab drops: dragging a tab onto this view adds it to favorites.
class AvoraFavoritesView : public views::View,
                           public FavoritesManager::Observer,
                           public TabStripModelObserver,
                           public avora::WindowSpaceState::Observer {
  METADATA_HEADER(AvoraFavoritesView, views::View)

 public:
  // Favicon display size in DIP.
  static constexpr int kFaviconSize = 32;
  // Inset inside each button's rounded-rect background.
  static constexpr int kButtonHInset = 6;
  static constexpr int kButtonVInset = 5;
  // Minimum button size (favicon + insets). Width may grow via layout.
  static constexpr int kButtonSize = kFaviconSize + kButtonHInset * 2;
  static constexpr int kButtonHeight = kFaviconSize + kButtonVInset * 2;
  // Corner radius for button background.
  static constexpr int kButtonRadius = 8;
  // Max icons per row before wrapping.
  static constexpr int kMaxPerRow = 4;
  // Padding between buttons.
  static constexpr int kIconPadding = 4;
  // Vertical padding above/below the grid.
  static constexpr int kVerticalPadding = 4;
  // Minimum height for the drop-zone placeholder when empty during a drag.
  static constexpr int kDropPlaceholderHeight = 42;

  AvoraFavoritesView(BrowserWindowInterface* browser,
                     avora::WindowSpaceState* window_space_state);
  AvoraFavoritesView(const AvoraFavoritesView&) = delete;
  AvoraFavoritesView& operator=(const AvoraFavoritesView&) = delete;
  ~AvoraFavoritesView() override;

  // FavoritesManager::Observer:
  void OnFavoritesChanged() override;

  // avora::WindowSpaceState::Observer:
  void OnWindowActiveSpaceChanged(const std::string& space_id) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(TabStripModel* tab_strip_model,
                              const TabStripModelChange& change,
                              const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;

  // views::View drop target overrides for drag-to-favorite support:
  bool GetDropFormats(
      int* formats,
      std::set<ui::ClipboardFormatType>* format_types) override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  views::View::DropCallback GetDropCallback(
      const ui::DropTargetEvent& event) override;

  // views::View:
  void OnPaintBackground(gfx::Canvas* canvas) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

  // Rebuild the grid from the current favorites list.
  void RebuildGrid();

  FavoritesManager* GetFavoritesManager() const;

  // Called by TabDragHandlerImpl when a tab drag begins/ends.
  void SetDragActive(bool active);
  // Called by TabDragHandlerImpl when the cursor enters/leaves this view.
  void SetDropHighlighted(bool highlighted);
  // Called by TabDragHandlerImpl on every mouse move during drag to update
  // the insertion gap position. Existing favicons slide apart to make room.
  void UpdateInsertionGap(const gfx::Point& screen_point);
  // Clears the insertion gap (e.g. when cursor leaves favorites).
  void ClearInsertionGap();
  // Returns the current insertion index, or -1 if no gap is shown.
  int insertion_gap_index() const { return insertion_gap_index_; }
  bool is_drag_active() const { return is_drag_active_; }

 private:
  // Called when a favorite button is clicked.
  void OnFavoriteClicked(const std::string& url);

  // Called when a favorite button is right-clicked.
  void OnFavoriteContextMenu(const std::string& url,
                             const gfx::Point& screen_point);

  // Loads the favicon for a FavoriteButton.
  void LoadFavicon(FavoriteButton* button);

  // Updates which FavoriteButton shows the active highlight based on the
  // currently active tab's URL.
  void UpdateActiveState();

  // On startup, ensure every favorite has a corresponding open tab.
  // Creates background tabs for any favorites whose tabs are missing.
  void EnsureFavoriteTabsExist();

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<avora::WindowSpaceState> window_space_state_ = nullptr;
  std::unique_ptr<FavoritesManager> favorites_manager_;

  // Raw pointer to the inner grid layout (owned by AnimatingLayoutManager).
  raw_ptr<views::LayoutManagerBase> grid_layout_ = nullptr;

  // Context menu state (kept alive for the duration of the menu).
  std::unique_ptr<ui::SimpleMenuModel::Delegate> context_menu_delegate_;
  std::unique_ptr<ui::SimpleMenuModel> context_menu_model_;
  std::unique_ptr<views::MenuRunner> context_menu_runner_;

  base::CancelableTaskTracker cancelable_task_tracker_;

  // True when a tab drag session is active (for showing drop placeholder).
  bool is_drag_active_ = false;
  // True when the cursor is hovering over this view during a drag.
  bool is_drop_highlighted_ = false;
  // Grid slot index where the insertion gap is shown (-1 = none).
  int insertion_gap_index_ = -1;

  base::WeakPtrFactory<AvoraFavoritesView> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_FAVORITES_VIEW_H_
