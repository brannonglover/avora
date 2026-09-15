// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_IMPORTED_SECTION_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_IMPORTED_SECTION_VIEW_H_

#include <memory>
#include <set>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "chrome/browser/avora/avora_imported_link_store.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/view.h"

class BrowserWindowInterface;

namespace ui {
class OSExchangeData;
}  // namespace ui

namespace gfx {
class Canvas;
class ImageSkia;
}  // namespace gfx

namespace views {
class ImageView;
class Label;
}  // namespace views

namespace avora {

// Custom clipboard format for imported link drag payloads.
// Contains pickled source_id + item_id for stable identity resolution.
const ui::ClipboardFormatType& GetImportedLinkClipboardFormatType();

// A link row inside the Imported section: favicon + title.
// Left-click opens URL in a new foreground tab; middle-click opens in
// background.  Right-click shows a context menu.  Left-drag initiates
// a drag-to-Favorites operation.
class ImportedLinkRow : public views::View {
  METADATA_HEADER(ImportedLinkRow, views::View)

 public:
  using ClickCallback = base::RepeatingCallback<void(const std::string& url,
                                                     bool background)>;
  using ContextCallback =
      base::RepeatingCallback<void(const std::string& source_id,
                                   const std::string& item_id,
                                   const std::string& url,
                                   const std::string& title,
                                   const gfx::Point& screen_point)>;

  ImportedLinkRow(const std::string& source_id,
                  const std::string& item_id,
                  const std::string& url,
                  const std::string& title,
                  int depth,
                  ClickCallback on_click,
                  ContextCallback on_context);
  ~ImportedLinkRow() override;

  const std::string& source_id() const { return source_id_; }
  const std::string& item_id() const { return item_id_; }
  const std::string& url() const { return url_; }
  void SetFavicon(const gfx::ImageSkia& icon);

  // views::View:
  void OnPaintBackground(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseCaptureLost() override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

 private:
  void MaybeStartDrag(const ui::MouseEvent& event);

  std::string source_id_;
  std::string item_id_;
  std::string url_;
  std::string title_;
  raw_ptr<views::ImageView> favicon_view_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  ClickCallback on_click_;
  ContextCallback on_context_;
  bool hovered_ = false;

  // Drag state: tracks left-button press for threshold-based drag initiation.
  bool left_pressed_ = false;
  bool drag_started_ = false;
  gfx::Point press_point_;
};

// A folder header row: disclosure chevron + folder name.
// Click to expand/collapse.
class ImportedFolderRow : public views::View {
  METADATA_HEADER(ImportedFolderRow, views::View)

 public:
  ImportedFolderRow(const std::string& source_id,
                    const std::string& item_id,
                    const std::string& title,
                    bool expanded,
                    int depth,
                    base::RepeatingCallback<void(const std::string&,
                                                 const std::string&)>
                        on_toggle);
  ~ImportedFolderRow() override;

  // views::View:
  void OnPaintBackground(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

 private:
  std::string source_id_;
  std::string item_id_;
  bool expanded_ = true;
  bool hovered_ = false;
  base::RepeatingCallback<void(const std::string&, const std::string&)>
      on_toggle_;
};

// Section header ("Imported") with disclosure chevron.  Click to
// expand/collapse the entire section.
class ImportedSectionHeader : public views::View {
  METADATA_HEADER(ImportedSectionHeader, views::View)

 public:
  explicit ImportedSectionHeader(
      bool expanded,
      base::RepeatingCallback<void()> on_toggle);
  ~ImportedSectionHeader() override;

  void SetExpanded(bool expanded);

  // views::View:
  void OnPaintBackground(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

 private:
  bool expanded_ = false;
  bool hovered_ = false;
  raw_ptr<views::Label> label_ = nullptr;
  base::RepeatingCallback<void()> on_toggle_;
};

// Source sub-header: "Chrome — Personal", "Firefox — Default", etc.
// Supports right-click context menu for source-level actions.
class ImportedSourceHeader : public views::View {
  METADATA_HEADER(ImportedSourceHeader, views::View)

 public:
  using ContextCallback =
      base::RepeatingCallback<void(const std::string& source_id,
                                   const gfx::Point& screen_point)>;

  ImportedSourceHeader(const std::string& source_id,
                       const std::string& browser,
                       const std::string& profile_name,
                       ContextCallback on_context);
  ~ImportedSourceHeader() override;

  const std::string& source_id() const { return source_id_; }

  // views::View:
  bool OnMousePressed(const ui::MouseEvent& event) override;

 private:
  std::string source_id_;
  ContextCallback on_context_;
};

// ─────────────────────────────────────────────────────────────────────────────
// The Imported section, positioned between Pinned and daily tabs.
//
// Displays imported bookmarks from external browsers, filtered by the
// current window's active Space.  The section is collapsed by default and
// feels visually secondary to Favorites and Pinned.
//
// Expansion/collapse state is per-window and ephemeral (reset on restart).
// ─────────────────────────────────────────────────────────────────────────────
class AvoraImportedSectionView : public views::View,
                                 public ImportedLinkStore::Observer,
                                 public WindowSpaceState::Observer {
  METADATA_HEADER(AvoraImportedSectionView, views::View)

 public:
  static constexpr int kRowHeight = 32;
  static constexpr int kFaviconSize = 16;
  static constexpr int kHPadding = 16;
  static constexpr int kIndentPerLevel = 14;
  static constexpr int kHeaderHeight = 30;
  static constexpr int kSourceHeaderHeight = 26;

  AvoraImportedSectionView(BrowserWindowInterface* browser,
                           WindowSpaceState* window_space_state);
  AvoraImportedSectionView(const AvoraImportedSectionView&) = delete;
  AvoraImportedSectionView& operator=(const AvoraImportedSectionView&) = delete;
  ~AvoraImportedSectionView() override;

  // ImportedLinkStore::Observer:
  void OnImportedLinksChanged() override;

  // WindowSpaceState::Observer:
  void OnWindowActiveSpaceChanged(const std::string& space_id) override;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

 private:
  // Rebuild the entire view from the current store and space state.
  // Posted asynchronously to avoid use-after-free from nested observer
  // callbacks.
  void ScheduleRebuild();
  void Rebuild();

  // Adds folder and link rows for a subtree rooted at |parent_id|
  // within |source|.
  void BuildSubtree(const ImportedSource& source,
                    const std::string& parent_id,
                    int depth);

  void OnSectionToggle();
  void OnImportClicked();
  void OnFolderToggle(const std::string& source_id,
                      const std::string& item_id);

  // ── Navigation ──────────────────────────────────────────────────────────
  // Opens |url| in a new tab in the originating browser window.
  // |background| controls foreground vs background disposition.
  void OnLinkClicked(const std::string& url, bool background);

  // ── Context menu ────────────────────────────────────────────────────────
  void OnLinkContextMenu(const std::string& source_id,
                         const std::string& item_id,
                         const std::string& url,
                         const std::string& title,
                         const gfx::Point& screen_point);
  void ExecuteContextMenuCommand(int command_id);

  // Loads a favicon for a link row.
  void LoadFavicon(ImportedLinkRow* row);

  // Returns whether a folder is expanded in this window.
  bool IsFolderExpanded(const std::string& source_id,
                        const std::string& item_id) const;

  // Expand the section to reveal imported content.  Called after import
  // to give the user immediate visibility.
  void ExpandSection();

  // Expand the section, reveal the given source, and scroll it into view.
  // Used by the "View Imported" post-import action.
  void RevealSource(const std::string& source_id);

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<WindowSpaceState> window_space_state_ = nullptr;
  std::unique_ptr<ImportedLinkStore> store_;

  // Section-level expand/collapse.  Collapsed by default.
  bool section_expanded_ = false;

  // Per-window, ephemeral folder expansion state.
  // Key: "source_id:item_id".  Present = expanded.
  std::set<std::string> expanded_folders_;

  bool rebuild_pending_ = false;
  base::CancelableTaskTracker cancelable_task_tracker_;

  // ── Link context menu state ─────────────────────────────────────────────
  std::unique_ptr<ui::SimpleMenuModel::Delegate> context_menu_delegate_;
  std::unique_ptr<ui::SimpleMenuModel> context_menu_model_;
  std::unique_ptr<views::MenuRunner> context_menu_runner_;

  // Stashed during context menu display for command execution.
  std::string context_source_id_;
  std::string context_item_id_;
  std::string context_url_;
  std::string context_title_;

  // ── Source context menu ────────────────────────────────────────────────
  void OnSourceContextMenu(const std::string& source_id,
                           const gfx::Point& screen_point);
  void ExecuteSourceContextMenuCommand(int command_id);

  std::unique_ptr<ui::SimpleMenuModel::Delegate>
      source_context_menu_delegate_;
  std::unique_ptr<ui::SimpleMenuModel> source_context_menu_model_;
  std::unique_ptr<views::MenuRunner> source_context_menu_runner_;
  std::string source_context_source_id_;

  base::WeakPtrFactory<AvoraImportedSectionView> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_IMPORTED_SECTION_VIEW_H_
