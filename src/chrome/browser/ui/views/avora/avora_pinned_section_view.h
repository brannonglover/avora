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
#include "components/tabs/public/tab_interface.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"
#include "url/gurl.h"

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
class BoxLayout;
class ImageView;
class Label;
class Textfield;
}  // namespace views

namespace avora {

class AvoraPinnedSectionView;

// Shared drag-to-reorder behavior for the section's two item row types.
//
// A row's identity for reordering is the kPinned item id behind it plus the
// folder currently holding it; Rebuild() hands both to SetDragContext(), so
// neither row type has to work out where it sits.  A row with no item id --
// a natively pinned tab that never got a kPinned record -- is not draggable,
// because there is no persisted position to rewrite.
//
// A plain left click is delivered on release rather than on press: until the
// pointer has moved far enough there is no telling a click from the start of
// a drag.  Presses a row wants for itself (right-click menus, double-click
// rename, a row in mid-rename) are still handled on press, by overriding
// OnMousePressed and deferring to this class only for what it did not
// consume.
class PinnedRowView : public views::View {
  METADATA_HEADER(PinnedRowView, views::View)

 public:
  PinnedRowView();
  ~PinnedRowView() override;

  const std::string& drag_item_id() const { return drag_item_id_; }
  const std::string& drag_folder_id() const { return drag_folder_id_; }

  // |folder_id| is the row's container; empty for a top-level row.
  void SetDragContext(const std::string& item_id,
                      const std::string& folder_id);

  // Marks this row as the one currently being dragged, which both row types
  // paint as a lifted card (PaintLiftedRowBackground).
  void SetDragged(bool dragged);
  bool is_dragged() const { return is_dragged_; }

  // Re-indents the row to the depth of the container it is currently over,
  // so a row dragged into or out of a folder reads as a member of the one it
  // would join before it is dropped.
  void SetRowIndent(bool in_folder);

  // views::View:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseCaptureLost() override;

 protected:
  // Runs the row's ordinary activation.  May delete this row: activating
  // materializes or selects a tab, which rebuilds the whole section, so
  // nothing may touch a member after calling it.
  virtual void PerformClickAction() = 0;

  // Both row types lay out with a horizontal BoxLayout whose leading inset
  // carries the row's depth; handing it over here is what lets SetRowIndent()
  // change that depth mid-drag.  |in_folder| is the depth it was built with.
  void SetRowLayout(views::BoxLayout* layout, bool in_folder);

 private:
  AvoraPinnedSectionView* GetSection();

  std::string drag_item_id_;
  std::string drag_folder_id_;

  raw_ptr<views::BoxLayout> row_layout_ = nullptr;
  bool in_folder_ = false;

  gfx::Point press_point_;
  // A left press landed here and has not resolved into a click or a drag yet.
  bool press_pending_ = false;
  bool dragging_ = false;
  bool is_dragged_ = false;
};

// A single folder header row: folder icon + name label, click to toggle.
// Double-click enters inline rename mode with a textfield.  Dragging it
// moves the whole folder -- header and members together -- through the
// section's top-level sequence; folders do not nest, so a folder's only
// drop targets are the positions between pins and other folders.
//
// Identified by the folder's stable PinnedFolder::id, never by list
// position: Rebuild() recreates every row on any change, so a stale index
// captured before a concurrent mutation (e.g. another window removing a
// different folder) could otherwise resolve to the wrong folder.
class FolderHeaderView : public PinnedRowView,
                         public views::TextfieldController {
  METADATA_HEADER(FolderHeaderView, PinnedRowView)

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

 protected:
  // PinnedRowView:
  void PerformClickAction() override;

 private:
  void CommitRename();
  void CancelRename();

  std::string folder_id_;
  // Drives which glyph the icon paints.  Set once at construction and never
  // mutated: AvoraPinnedSectionView::Rebuild() recreates every row whenever
  // the folder list changes, expansion included, so a header always carries
  // the state it was built with.
  bool expanded_ = false;
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
class FolderTabRow : public PinnedRowView {
  METADATA_HEADER(FolderTabRow, PinnedRowView)

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

 protected:
  // PinnedRowView:
  void PerformClickAction() override;

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

// The favicon slot of a live pinned row, and the row's entire "this tab is
// not showing the page it was pinned for" signal.
//
// Drawing the favicon is the ordinary half.  The other half exists because a
// pinned item keeps its own persisted target while the tab materializing it
// is free to browse anywhere (PinnedItemEntry::url is never rewritten by
// navigation), so the two can disagree.  When they do, the icon says so
// without saying anything and without adding anything: it keeps drawing the
// *target's* favicon, unchanged and still 16dip -- a pin's identity does not
// change just because its page did -- and lifts it off the row with a drop
// shadow.  Under the pointer that same favicon becomes a return arrow, and
// clicking it takes the tab back.  While the tab is on its target this is an
// inert favicon: it sits flat and lets every click fall through to the row
// underneath, so the ordinary activate-and-rename behaviour is untouched.
//
// The view is sized for the shadow, not for the favicon, and keeps that size
// in both states: the favicon must not move when a tab wanders off or comes
// home.  PinnedTabRow's layout takes the difference back out of its own
// padding so the favicon lands exactly where FolderTabRow's does.
class PinnedRowIcon : public views::View {
  METADATA_HEADER(PinnedRowIcon, views::View)

 public:
  explicit PinnedRowIcon(base::RepeatingClosure on_return);
  ~PinnedRowIcon() override;

  // How much wider and taller than the favicon this view is, so the shadow
  // has room inside the bounds views clips it to.  PinnedTabRow subtracts it
  // from its own leading padding and icon-to-title spacing.
  static int ShadowInset();

  // |icon| is expected pre-sized to AvoraPinnedSectionView::kFaviconSize.
  void SetFavicon(const gfx::ImageSkia& icon);

  void SetAwayFromTarget(bool away);

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  views::View* GetTooltipHandlerForPoint(const gfx::Point& point) override;

 private:
  gfx::ImageSkia icon_;
  // |icon_| with the away shadow baked in, built once per favicon rather
  // than per paint.  Null until a favicon arrives.
  gfx::ImageSkia shadowed_icon_;
  bool away_ = false;
  base::RepeatingClosure on_return_;
};

// A standalone pinned tab row (not inside a folder): favicon + title.
// Click to activate; right-click for context menu with "Unpin Tab".
// Double-click to rename via an inline textfield.  Hovering shows a minus
// where a daily tab shows its close "x": it closes the live tab but keeps the
// pin, which is why it is not drawn as an "x".
class PinnedTabRow : public PinnedRowView,
                     public views::TextfieldController {
  METADATA_HEADER(PinnedTabRow, PinnedRowView)

 public:
  using RenameCallback = base::RepeatingCallback<void(
      content::WebContents*, const std::u16string&)>;

  // |on_return| runs when the row's favicon is clicked while the tab has
  // left the pinned item's target; see SetPinnedTarget() and PinnedRowIcon.
  //
  // |on_close| runs when the row's minus is clicked.  Null means no minus
  // (the drag stand-in, which nothing can click).
  //
  // |in_folder| indents the row to line up with the unmaterialized folder
  // members drawn by FolderTabRow.  A folder member renders as this row type
  // as soon as it has a live tab (Rebuild() section 2), so without this the
  // same item visibly jumps flush-left the moment it is materialized, and a
  // folder holding one of each renders its two children at different depths.
  PinnedTabRow(
      content::WebContents* contents,
      base::RepeatingCallback<void(content::WebContents*)> on_click,
      base::RepeatingCallback<void(content::WebContents*,
                                   const gfx::Point&)> on_context,
      RenameCallback on_rename,
      base::RepeatingCallback<void(content::WebContents*)> on_return,
      base::RepeatingCallback<void(content::WebContents*)> on_close,
      bool in_folder = false);
  ~PinnedTabRow() override;

  content::WebContents* web_contents() const { return contents_; }

  // The pinned item's persisted navigation target, and whether the live tab
  // has since been navigated off it.  Rows whose tab backs no pinned item
  // (a natively pinned tab with no record yet) simply never get told, and
  // behave exactly as they did before this existed.
  void SetPinnedTarget(const GURL& target, bool away);

  // What this row's favicon should be loaded for: the target when there is
  // one, so a wandering tab keeps the identity it was pinned with.
  const GURL& target_url() const { return target_url_; }

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

 protected:
  // PinnedRowView:
  void PerformClickAction() override;

 private:
  void CancelRename();

  // Runs |on_return_| for this row's tab.  Bound into the favicon, which is
  // what the user actually clicks.
  void ReturnToTarget();

  // Runs |on_close_| for this row's tab.  Bound into the minus button.
  void CloseTab();

  // The minus shows while the row is hovered, as a daily tab's "x" does --
  // never mid-rename, where it would sit on the end of the textfield.
  void UpdateCloseButtonVisibility();

  raw_ptr<content::WebContents> contents_;
  raw_ptr<PinnedRowIcon> favicon_view_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Textfield> rename_field_ = nullptr;
  raw_ptr<views::Button> close_button_ = nullptr;
  base::RepeatingCallback<void(content::WebContents*)> on_click_;
  base::RepeatingCallback<void(content::WebContents*,
                               const gfx::Point&)> on_context_;
  RenameCallback on_rename_;
  base::RepeatingCallback<void(content::WebContents*)> on_return_;
  base::RepeatingCallback<void(content::WebContents*)> on_close_;
  GURL target_url_;
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

  // Avora: track a drag that originates outside this section -- a tab being
  // dragged up out of the daily list -- so it gets the same insertion line
  // the section's own row drags draw.  A row drag can preview itself by
  // reflowing the real rows (PreviewDropAt), but an incoming tab has no row
  // here yet, so the landing spot has to be drawn rather than occupied.
  void SetExternalDropPoint(const gfx::Point& screen_point);
  void ClearExternalDrop();

  // Avora: preview a tab dragged in from the daily list as a real row, so the
  // section reflows around it exactly as it does for one of its own rows
  // (PreviewDropAt moves views, and an incoming tab otherwise has none here).
  // The stand-in is built from the dragged tab's own WebContents, which is
  // what a PinnedTabRow is made of anyway, so the preview is the finished row
  // rather than an imitation of one.
  //
  // Nothing is persisted until the drop: the stand-in stands in the top-level
  // sequence under a provisional id that EndExternalRowDrag() swaps for the
  // id of the pinned item the caller creates on release.
  bool BeginExternalRowDrag(content::WebContents* contents);
  void UpdateExternalRowDrag(const gfx::Point& screen_point);
  // The stand-in only belongs in the list while the drag is actually over the
  // section.  Out of it -- on its way back to the daily list, or over the
  // favorites grid -- the row is hidden rather than parked somewhere, because
  // a row still holding a slot claims a placement the release would not make.
  // It is hidden and not removed so that coming back costs nothing.
  void SetExternalRowDragInSection(bool in_section);
  // |item_id| is the pinned item the caller has just created for the tab, and
  // is ignored when |commit| is false.
  void EndExternalRowDrag(bool commit, const std::string& item_id);
  bool has_external_row_drag() const {
    return !external_provisional_id_.empty();
  }
  std::string GetFolderIdAtScreenPoint(const gfx::Point& screen_point) const;
  void HighlightFolder(const std::string& folder_id);
  void ClearHighlight();

  // Drag-to-reorder within the section, driven by PinnedRowView.  This is a
  // separate gesture from the tab-strip drag the three methods above serve:
  // that one brings a daily tab *into* the section, this one only moves what
  // is already here -- a pinned row, or a folder header, which carries its
  // members with it.  |row| must be a child of this view; returns false if
  // it cannot be reordered, and the press then stays a plain click.
  bool BeginRowDrag(PinnedRowView* row);
  void UpdateRowDrag(const gfx::Point& screen_point);
  // |commit| is false when the gesture was cancelled (capture lost) rather
  // than released; either way the drag state is torn down.
  void EndRowDrag(bool commit);

 private:
  // Where a dragged row would land: a position within |folder_id| (empty for
  // top-level), counted with the dragged item already taken out of that
  // container -- what MoveItemToFolderAt() expects.  A top-level drop reads
  // its order from the arranged rows instead (BuildTopLevelSequence), so
  // |index| only matters when |folder_id| is set.
  struct DropTarget {
    std::string folder_id;
    int index = 0;
  };

  // One visible row of the section, in display order, tagged with the
  // container it belongs to.  Rebuilt from children() on demand rather than
  // cached: a cache would outlive the rows a rebuild deletes.
  struct RowSlot {
    raw_ptr<views::View> view = nullptr;
    bool is_header = false;
    // A header's own folder id; a row's containing folder ("" = top-level).
    std::string folder_id;
    // Empty for headers, and for rows with no kPinned record to reorder.
    std::string item_id;
  };

  // The drop the cursor is currently indicating.
  struct DropPosition {
    DropTarget target;
    // The cursor is over a folder header: the item joins that folder, and
    // the header highlights to say so.
    bool onto_folder_header = false;
    // The row the dragged one should sit directly in front of while it is
    // being previewed in place; null means "after everything".
    raw_ptr<views::View> before_view = nullptr;
  };

  // The rows a drop is measured against: every visible row except the one
  // being dragged, which is excluded because it is currently standing in the
  // slot it would land in rather than the one it came from.
  // Whether |view| is one of the views travelling with the current drag.
  bool IsDraggedView(const views::View* view) const;

  std::vector<RowSlot> BuildRowSlots() const;
  DropPosition ComputeDropPosition(int local_y) const;

  // A dragged folder measures itself against whole blocks -- a pin, or a
  // folder header together with its members -- rather than single rows, so
  // it steps over another folder in one move instead of wading through its
  // members, and can only ever land between blocks.  Folders do not nest.
  DropPosition ComputeFolderDropPosition(int local_y) const;

  // Previews the drop by moving the dragged row itself into the slot it would
  // land in, at that slot's depth.  The rows it displaces slide aside, so the
  // list under the cursor is already the list the drop would produce.
  void PreviewDropAt(const DropPosition& position);

  // One entry of the section's top-level sequence: a pinned item, or a
  // folder drawn at that point between items.
  struct TopLevelEntry {
    bool is_folder = false;
    std::string id;
  };

  // The top-level sequence exactly as the rows currently show it, dragged
  // row included at the position it is being previewed in.  Captured at drop
  // time, before anything is written, so what the drag showed is what gets
  // persisted -- including where the folders ended up relative to it.
  std::vector<TopLevelEntry> BuildTopLevelSequence() const;

  // Applies a finished drag, then rebuilds.  Posted rather than run from the
  // mouse release, because writing the new order reenters this view through
  // the managers' observers and deletes the row still dispatching that
  // release.
  // |item_id| is empty when what was dragged is a folder: its move lives
  // entirely in |sequence|, which already carries folder placement.
  void FinishRowDrag(bool commit,
                     const std::string& item_id,
                     const DropTarget& target,
                     const std::vector<TopLevelEntry>& sequence);
  void CommitRowDrag(const std::string& item_id,
                     const DropTarget& target,
                     const std::vector<TopLevelEntry>& sequence);

  // Writes |sequence| back: pinned-item order for the items, and each
  // folder's placement among them.
  void ApplyTopLevelSequence(const std::vector<TopLevelEntry>& sequence);

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
  void OnImportArcClicked();

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

  // The row's minus: closes the live tab and leaves the pinned item in place,
  // so the row falls back to its unmaterialized form.  Closing the tab that is
  // on screen hands focus back to the tab that was on screen before it, not
  // to whichever neighbour TabStripModel would pick.
  //
  // A tab with no pinned item behind it would lose its pin by closing, so it
  // is parked instead (see avora_parked_pin_view.h): it stays open and
  // pinned, and the content area shows a quote until the pin is clicked.
  void OnPinnedTabCloseRequested(content::WebContents* contents);

  // Activation history for OnPinnedTabCloseRequested(): every tab that loses
  // activation is moved to the front, so the front is "the tab before this
  // one".  Kept by handle so a closed tab simply stops resolving.
  void RememberDeactivatedTab(tabs::TabInterface* tab);

  // The most recently active tab, other than |closing|, that still lives in
  // this window and is drawn in the Space this window shows.  Null when none
  // qualifies, and the close then falls back to TabStripModel's own choice.
  tabs::TabInterface* FindPreviouslyActiveTab(tabs::TabInterface* closing);

  // Takes a pinned tab back to the target it was pinned for, and activates
  // it.  Bound into every live row's favicon; does nothing for a tab that is
  // already on its target.
  void OnPinnedTabReturnToTarget(content::WebContents* contents);

  // Tells |row| where its tab was pinned to point, and whether it is still
  // there -- the one place the live tab and the persisted item are compared,
  // so every live row agrees on what "away" means.
  void ApplyPinnedTargetState(PinnedTabRow* row);
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
  // Local-Y of the insertion line for an in-flight external drag, or -1 when
  // there is none to draw (no such drag, or it is aimed at a folder header,
  // where the header highlight already says where the tab lands).
  int external_drop_line_y_ = -1;
  // Non-empty while a tab from the daily list is being previewed as a row
  // here; the id its stand-in occupies the sequence under until the drop.
  std::string external_provisional_id_;
  std::string highlighted_folder_id_;

  // Row-reorder drag state (BeginRowDrag/UpdateRowDrag/EndRowDrag).
  bool row_drag_active_ = false;
  // Exactly one of these identifies what is moving: a pinned item, or a
  // folder.
  std::string row_drag_item_id_;
  std::string row_drag_folder_id_;
  // The views that travel together: one row for a pin, or a folder's header
  // followed by its member rows.
  std::vector<raw_ptr<PinnedRowView>> row_drag_views_;
  // False while the pointer is outside the section: the user is on their way
  // somewhere else, and releasing there must not reorder anything.
  bool row_drop_valid_ = false;
  DropTarget row_drop_target_;
  bool row_drop_onto_folder_ = false;
  // Where the dragged block sat before the preview started moving it -- the
  // row it stood in front of, or null for "last" -- so a drag that wanders
  // off the section can put it back.
  raw_ptr<views::View> row_drag_origin_before_ = nullptr;

  // Most recently deactivated first; see RememberDeactivatedTab().
  std::vector<tabs::TabHandle> activation_history_;

  // Set when something asked for a rebuild mid-drag.  Rebuilding then would
  // delete the very row the user is holding, so the request is folded into
  // the rebuild that follows the drop.
  bool rebuild_pending_ = false;

  base::WeakPtrFactory<AvoraPinnedSectionView> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_PINNED_SECTION_VIEW_H_
