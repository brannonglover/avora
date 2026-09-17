// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PINNED_SECTION_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PINNED_SECTION_VIEW_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "chrome/browser/avora/avora_pinned_folders.h"
#include "chrome/browser/avora/avora_pinned_items.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "chrome/browser/ui/tabs/tab_change_type.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class BrowserWindowInterface;
class TabStripModel;

namespace content {
class WebContents;
}  // namespace content

namespace gfx {
class Canvas;
class ImageSkia;
}  // namespace gfx

namespace views {
class ImageView;
class Label;
class Textfield;
}  // namespace views

namespace avora {

// A single folder header row: folder icon + name label, click to toggle.
// Double-click enters inline rename mode with a textfield.
class FolderHeaderView : public views::View,
                         public views::TextfieldController {
  METADATA_HEADER(FolderHeaderView, views::View)

 public:
  FolderHeaderView(int folder_index,
                   const std::string& name,
                   bool expanded,
                   base::RepeatingCallback<void(int)> on_toggle,
                   base::RepeatingCallback<void(int, const gfx::Point&)>
                       on_context,
                   base::RepeatingCallback<void(int, const std::string&)>
                       on_rename);
  ~FolderHeaderView() override;

  int folder_index() const { return folder_index_; }
  void SetHighlighted(bool highlighted);
  void BeginRename();

  // views::View:
  void OnPaintBackground(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  void CommitRename();
  void CancelRename();

  int folder_index_;
  bool highlighted_ = false;
  bool is_renaming_ = false;
  base::RepeatingCallback<void(int)> on_toggle_;
  base::RepeatingCallback<void(int, const gfx::Point&)> on_context_;
  base::RepeatingCallback<void(int, const std::string&)> on_rename_;
  raw_ptr<views::View> icon_spacer_ = nullptr;
  raw_ptr<views::Label> name_label_ = nullptr;
  raw_ptr<views::Textfield> rename_field_ = nullptr;
};

// A tab row inside a folder: indented favicon + title, click to activate.
class FolderTabRow : public views::View {
  METADATA_HEADER(FolderTabRow, views::View)

 public:
  FolderTabRow(int folder_index,
               const std::string& url,
               const std::string& title,
               base::RepeatingCallback<void(const std::string&)> on_click,
               base::RepeatingCallback<void(int, const std::string&,
                                            const gfx::Point&)> on_context);
  ~FolderTabRow() override;

  const std::string& url() const { return url_; }
  void SetFavicon(const gfx::ImageSkia& icon);

  // views::View:
  void OnPaintBackground(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

 private:
  int folder_index_;
  std::string url_;
  std::string title_;
  raw_ptr<views::ImageView> favicon_view_ = nullptr;
  base::RepeatingCallback<void(const std::string&)> on_click_;
  base::RepeatingCallback<void(int, const std::string&, const gfx::Point&)>
      on_context_;
};

// A standalone pinned tab row (not inside a folder): favicon + title.
// Click to activate; right-click for context menu with "Unpin Tab".
// Double-click to rename via an inline textfield.
class PinnedTabRow : public views::View,
                     public views::TextfieldController {
  METADATA_HEADER(PinnedTabRow, views::View)

 public:
  using RenameCallback = base::RepeatingCallback<void(
      content::WebContents*, const std::u16string&)>;

  PinnedTabRow(
      content::WebContents* contents,
      base::RepeatingCallback<void(content::WebContents*)> on_click,
      base::RepeatingCallback<void(content::WebContents*,
                                   const gfx::Point&)> on_context,
      RenameCallback on_rename);
  ~PinnedTabRow() override;

  content::WebContents* web_contents() const { return contents_; }
  void SetFavicon(const gfx::ImageSkia& icon);
  void UpdateTitle();
  void SetCustomTitle(const std::u16string& title);
  void StartRename();
  void CommitRename();

  // views::View:
  void OnPaintBackground(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  void CancelRename();
  raw_ptr<content::WebContents> contents_;
  raw_ptr<views::ImageView> favicon_view_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Textfield> rename_field_ = nullptr;
  base::RepeatingCallback<void(content::WebContents*)> on_click_;
  base::RepeatingCallback<void(content::WebContents*,
                               const gfx::Point&)> on_context_;
  RenameCallback on_rename_;
  bool is_renaming_ = false;
};

// The unified pinned section view showing both standalone pinned tabs
// and folders with their contained tabs. Sits between the favorites grid
// and the daily (unpinned) tab list.
// Standalone pinned tabs appear at the top, folders below.
// Right-clicking shows "New Folder". Supports drag-to-folder.
class AvoraPinnedSectionView : public views::View,
                               public PinnedFoldersManager::Observer,
                               public PinnedItemsManager::Observer,
                               public views::ContextMenuController,
                               public TabStripModelObserver,
                               public avora::WindowSpaceState::Observer {
  METADATA_HEADER(AvoraPinnedSectionView, views::View)

 public:
  static constexpr int kRowHeight = 38;
  static constexpr int kFaviconSize = 16;
  static constexpr int kIndent = 14;
  static constexpr int kHPadding = 16;

  AvoraPinnedSectionView(BrowserWindowInterface* browser,
                         avora::WindowSpaceState* window_space_state);
  AvoraPinnedSectionView(const AvoraPinnedSectionView&) = delete;
  AvoraPinnedSectionView& operator=(const AvoraPinnedSectionView&) = delete;
  ~AvoraPinnedSectionView() override;

  // PinnedFoldersManager::Observer:
  void OnPinnedFoldersChanged() override;

  // PinnedItemsManager::Observer:
  void OnPinnedItemsChanged() override;

  // avora::WindowSpaceState::Observer:
  void OnWindowActiveSpaceChanged(const std::string& space_id) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void OnPaintBackground(gfx::Canvas* canvas) override;

  // Rebuild the view from the current folders data and pinned tabs.
  void Rebuild();

  PinnedFoldersManager* GetFoldersManager() const;
  PinnedItemsManager* GetPinnedItemsManager() const;

  // Drag-and-drop support: highlight a folder during tab drag.
  void SetDragActive(bool active);
  void SetDropHighlighted(bool highlighted);
  int GetFolderIndexAtScreenPoint(const gfx::Point& screen_point) const;
  void HighlightFolder(int folder_index);
  void ClearHighlight();

 private:
  void OnFolderToggle(int folder_index);
  void OnFolderRename(int folder_index, const std::string& new_name);
  void OnFolderContextMenu(int folder_index, const gfx::Point& screen_point);
  void OnTabClicked(const std::string& url);
  void OnTabContextMenu(int folder_index,
                        const std::string& url,
                        const gfx::Point& screen_point);
  void LoadFavicon(FolderTabRow* row);

  // Persisted pinned item (SidebarItemType::kPinned) with no live tab yet in
  // this window/Space. Activates an existing materialized tab if one already
  // exists here, else lazily creates one from the item's persisted URL --
  // mirrors AvoraFavoritesView::OnFavoriteClicked exactly.
  void OnPersistentPinnedItemClicked(const std::string& url);

  // Standalone pinned tab handlers.
  void OnPinnedTabClicked(content::WebContents* contents);
  void OnPinnedTabRenamed(content::WebContents* contents,
                          const std::u16string& new_title);
  void OnPinnedTabContextMenu(content::WebContents* contents,
                              const gfx::Point& screen_point);
  void LoadPinnedTabFavicon(PinnedTabRow* row);

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<avora::WindowSpaceState> window_space_state_ = nullptr;
  raw_ptr<TabStripModel> tab_strip_model_ = nullptr;
  std::unique_ptr<PinnedFoldersManager> folders_manager_;
  std::unique_ptr<PinnedItemsManager> pinned_items_manager_;

  // Context menu state.
  std::unique_ptr<ui::SimpleMenuModel::Delegate> context_menu_delegate_;
  std::unique_ptr<ui::SimpleMenuModel> context_menu_model_;
  std::unique_ptr<views::MenuRunner> context_menu_runner_;

  base::CancelableTaskTracker cancelable_task_tracker_;

  bool is_drag_active_ = false;
  bool is_drop_highlighted_ = false;
  int highlighted_folder_index_ = -1;

  base::WeakPtrFactory<AvoraPinnedSectionView> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PINNED_SECTION_VIEW_H_
