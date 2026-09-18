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
#include "chrome/browser/avora/avora_space_manager.h"
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
//
// Identified by the folder's stable PinnedFolder::id, never by list
// position: Rebuild() recreates every row on any change, so a stale index
// captured before a concurrent mutation (e.g. another window removing a
// different folder) could otherwise resolve to the wrong folder.
class FolderHeaderView : public views::View,
                         public views::TextfieldController {
  METADATA_HEADER(FolderHeaderView, views::View)

 public:
  FolderHeaderView(
      const std::string& folder_id,
      const std::string& name,
      bool expanded,
      base::RepeatingCallback<void(const std::string&)> on_toggle,
      base::RepeatingCallback<void(const std::string&, const gfx::Point&)>
          on_context,
      base::RepeatingCallback<void(const std::string&, const std::string&)>
          on_rename);
  ~FolderHeaderView() override;

  const std::string& folder_id() const { return folder_id_; }
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

  std::string folder_id_;
  bool highlighted_ = false;
  bool is_renaming_ = false;
  base::RepeatingCallback<void(const std::string&)> on_toggle_;
  base::RepeatingCallback<void(const std::string&, const gfx::Point&)>
      on_context_;
  base::RepeatingCallback<void(const std::string&, const std::string&)>
      on_rename_;
  raw_ptr<views::View> icon_spacer_ = nullptr;
  raw_ptr<views::Label> name_label_ = nullptr;
  raw_ptr<views::Textfield> rename_field_ = nullptr;
};

// A tab row for a pinned item with no live tab yet in this window: indented
// favicon + title, click to lazily materialize (see
// AvoraPinnedSectionView::OnPersistentPinnedItemClicked). Used both for
// top-level unmaterialized items (folder_id empty) and unmaterialized folder
// members (folder_id set).
//
// Identified by the item's stable PinnedItemEntry::id for click/context-menu
// dispatch -- url/title are display-only, resolved from the item itself, and
// are never compared against to find a live tab (that is
// FindMaterializedPinnedItemTab()'s job, keyed on id).
class FolderTabRow : public views::View {
  METADATA_HEADER(FolderTabRow, views::View)

 public:
  FolderTabRow(const std::string& folder_id,
              const std::string& item_id,
              const std::string& url,
              const std::string& title,
              base::RepeatingCallback<void(std::string)> on_click,
              base::RepeatingCallback<void(const std::string&,
                                           const std::string&,
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
  std::string folder_id_;
  std::string item_id_;
  std::string url_;
  std::string title_;
  raw_ptr<views::ImageView> favicon_view_ = nullptr;
  // By value, not by const&: running this destroys the row that owns
  // item_id_ (see OnMousePressed).
  base::RepeatingCallback<void(std::string)> on_click_;
  base::RepeatingCallback<void(const std::string&, const std::string&,
                               const gfx::Point&)>
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

  // Adds an empty folder to the active Space and drops its header row
  // straight into inline rename, so the user names it in place rather than
  // hunting for a folder called "New Folder".  Used both by this section's
  // own context menus and by the spaces bar "+" menu, which has no folder
  // rows of its own to rename.
  void CreateFolderAndBeginRename();

  PinnedFoldersManager* GetFoldersManager() const;
  PinnedItemsManager* GetPinnedItemsManager() const;

  // Drag-and-drop support: highlight a folder during tab drag.
  void SetDragActive(bool active);
  void SetDropHighlighted(bool highlighted);
  std::string GetFolderIdAtScreenPoint(const gfx::Point& screen_point) const;
  void HighlightFolder(const std::string& folder_id);
  void ClearHighlight();

 private:
  void OnFolderToggle(const std::string& folder_id);
  void OnFolderRename(const std::string& folder_id,
                      const std::string& new_name);
  void OnFolderContextMenu(const std::string& folder_id,
                           const gfx::Point& screen_point);
  void LoadFavicon(FolderTabRow* row);

  // Context menu for an unmaterialized folder member (a FolderTabRow with a
  // non-empty folder_id): "Remove from Folder" moves the item back to
  // top-level (MoveItemToFolder(item_id, "")) without touching any live tab
  // or its kPinned record.
  void OnFolderItemContextMenu(const std::string& folder_id,
                               const std::string& item_id,
                               const gfx::Point& screen_point);

  // Context menu for a persisted pinned item that has no live tab in this
  // window (the top-level rows built by section 1b of Rebuild()).
  void OnPersistentPinnedItemContextMenu(const std::string& item_id,
                                         const gfx::Point& screen_point);

  // Builds the "Move to Space" submenu for |current_space_id|, or nullptr
  // when there is nowhere to move to (a single Space).  The returned model
  // is its own delegate and runs |on_pick| with the chosen Space id.
  std::unique_ptr<ui::SimpleMenuModel> BuildMoveToSpaceSubMenu(
      const std::string& current_space_id,
      base::RepeatingCallback<void(const std::string&)> on_pick);

  // Opens the bookmark import dialog from the section context menu.
  void OnImportBookmarksClicked();

  // Hands a live pinned tab and the kPinned item backing it to |space_id|.
  // Never closes or navigates the page: the tab simply stops being drawn in
  // this Space and starts being drawn in the destination one.
  void MovePinnedTabToSpace(content::WebContents* contents,
                            const std::string& space_id);

  // The no-live-tab counterpart: reassigns the persisted kPinned item alone.
  void MovePinnedItemToSpace(const std::string& item_id,
                             const std::string& space_id);

  // Persisted pinned item (SidebarItemType::kPinned) with no live tab yet in
  // this window/Space -- top-level or a folder member, identical handling
  // either way (no folder-specific materialization path). Activates an
  // existing materialized tab if one already exists here, else lazily
  // creates one from the item's persisted URL -- mirrors
  // AvoraFavoritesView::OnFavoriteClicked exactly.
  // |item_id| is taken by value on purpose: materializing inserts a tab,
  // which reenters this view through OnTabStripModelChanged() -> Rebuild()
  // and destroys the row that supplied the id, mid-call.
  void OnPersistentPinnedItemClicked(std::string item_id);

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
  std::unique_ptr<SpaceManager> space_manager_;

  // Context menu state.
  std::unique_ptr<ui::SimpleMenuModel::Delegate> context_menu_delegate_;
  std::unique_ptr<ui::SimpleMenuModel> context_menu_model_;
  // Submenu models are referenced by the parent model as bare pointers, so
  // they have to outlive the menu just as the parent model does.
  std::unique_ptr<ui::SimpleMenuModel> move_to_space_submenu_;
  std::unique_ptr<views::MenuRunner> context_menu_runner_;

  base::CancelableTaskTracker cancelable_task_tracker_;

  bool is_drag_active_ = false;
  bool is_drop_highlighted_ = false;
  std::string highlighted_folder_id_;

  base::WeakPtrFactory<AvoraPinnedSectionView> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PINNED_SECTION_VIEW_H_
