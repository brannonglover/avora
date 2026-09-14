// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LIVE_FOLDERS_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LIVE_FOLDERS_VIEW_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/avora/avora_live_folder.h"
#include "chrome/browser/avora/avora_live_folder_service.h"
#include "chrome/browser/avora/avora_live_folder_store.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "chrome/browser/ui/tabs/tab_change_type.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/view.h"

class BrowserWindowInterface;

namespace gfx {
class Canvas;
}  // namespace gfx

namespace views {
class Label;
}  // namespace views

namespace avora {

// Header row for a Live Folder: disclosure chevron, name, and -- when the
// folder is collapsed and has arrived at new items since the user last looked
// -- an unseen count badge.
class LiveFolderHeaderRow : public views::View {
  METADATA_HEADER(LiveFolderHeaderRow, views::View)

 public:
  LiveFolderHeaderRow(
      const std::string& folder_id,
      const std::string& name,
      bool expanded,
      bool syncing,
      int unseen_count,
      base::RepeatingCallback<void(const std::string&)> on_toggle,
      base::RepeatingCallback<void(const std::string&, const gfx::Point&)>
          on_context);
  ~LiveFolderHeaderRow() override;

  // views::View:
  void OnPaintBackground(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

 private:
  std::string folder_id_;
  bool expanded_ = true;
  bool syncing_ = false;
  int unseen_count_ = 0;
  bool hovered_ = false;

  base::RepeatingCallback<void(const std::string&)> on_toggle_;
  base::RepeatingCallback<void(const std::string&, const gfx::Point&)>
      on_context_;
};

// A single pull request row: status dot, title, and "repo #number" subtitle.
class LiveFolderItemRow : public views::View {
  METADATA_HEADER(LiveFolderItemRow, views::View)

 public:
  LiveFolderItemRow(
      const std::string& folder_id,
      const LiveFolderItem& item,
      base::RepeatingCallback<void(const std::string&, const std::string&)>
          on_click,
      base::RepeatingCallback<void(const std::string&, const std::string&,
                                   const gfx::Point&)> on_context);
  ~LiveFolderItemRow() override;

  // views::View:
  void OnPaintBackground(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

 private:
  std::string folder_id_;
  std::string external_id_;
  std::string url_;
  LiveFolderItemStatus status_ = LiveFolderItemStatus::kOpen;

  // Unseen rows get a subtle emphasis so a newly-arrived pull request is
  // findable after the user expands the folder the badge pointed them at.
  bool unseen_ = false;
  bool hovered_ = false;

  base::RepeatingCallback<void(const std::string&, const std::string&)>
      on_click_;
  base::RepeatingCallback<void(const std::string&, const std::string&,
                               const gfx::Point&)>
      on_context_;
};

// The Live Folders section of the sidebar.
//
// Renders whatever LiveFolderStore holds for the active Space and nothing
// else: it never fetches, and it never edits item contents.  User actions are
// forwarded to LiveFolderService, whose writes come back as store
// notifications.  That one-way flow is what keeps "the provider owns the
// items" true in the UI layer too.
//
// The view takes zero height when the active Space has no Live Folders, so
// Spaces that never connect an account are unaffected.
class AvoraLiveFoldersView : public views::View,
                             public LiveFolderStore::Observer,
                             public LiveFolderService::Observer,
                             public TabStripModelObserver,
                             public avora::WindowSpaceState::Observer {
  METADATA_HEADER(AvoraLiveFoldersView, views::View)

 public:
  static constexpr int kRowHeight = 38;
  static constexpr int kItemRowHeight = 44;
  static constexpr int kHPadding = 16;
  static constexpr int kIndent = 14;

  AvoraLiveFoldersView(BrowserWindowInterface* browser,
                       avora::WindowSpaceState* window_space_state);
  AvoraLiveFoldersView(const AvoraLiveFoldersView&) = delete;
  AvoraLiveFoldersView& operator=(const AvoraLiveFoldersView&) = delete;
  ~AvoraLiveFoldersView() override;

  // Creates the GitHub folder for the active Space and opens the connect
  // dialog.  Entry point for the "Connect GitHub" affordance elsewhere in the
  // UI.
  void StartGitHubConnectFlow();

  // LiveFolderStore::Observer:
  void OnLiveFoldersChanged() override;

  // LiveFolderService::Observer:
  void OnLiveFolderSyncStateChanged(const std::string& folder_id) override;

  // avora::WindowSpaceState::Observer:
  void OnWindowActiveSpaceChanged(const std::string& space_id) override;

  // TabStripModelObserver: watched purely to notice the user visiting a
  // provider's site, which is what makes the folder appear in the first place.
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

  void Rebuild();

 private:
  // Rebuilds on a fresh task rather than immediately.
  //
  // Every rebuild path originates in a row's own event handler -- a click that
  // toggles the folder, opens a pull request, or runs a context menu command.
  // Rebuilding synchronously would delete that row while its handler is still
  // on the stack, so the work is posted instead.
  void ScheduleRebuild();

  void OnFolderToggle(const std::string& folder_id);
  void OnFolderContextMenu(const std::string& folder_id,
                           const gfx::Point& screen_point);
  void OnItemClicked(const std::string& folder_id,
                     const std::string& external_id);
  void OnItemContextMenu(const std::string& folder_id,
                         const std::string& external_id,
                         const gfx::Point& screen_point);

  // Opens |url| in the tab that already shows it, or a new one.
  void OpenUrl(const std::string& url);

  void ShowConnectDialog(const std::string& folder_id);

  // Toggles one involvement filter and re-syncs.
  void ToggleQueryFilter(const std::string& folder_id, int command_id);

  // Adds a non-interactive row explaining why a folder has no items.
  void AddMessageRow(const std::u16string& text);

  // Adds the "Connect GitHub" call to action.
  void AddConnectRow(const std::string& folder_id);

  // Reports the active tab's host to the service for provider detection.
  void ReportActiveHost();

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<avora::WindowSpaceState> window_space_state_ = nullptr;
  raw_ptr<TabStripModel> tab_strip_model_ = nullptr;
  raw_ptr<LiveFolderService> service_ = nullptr;
  raw_ptr<LiveFolderStore> store_ = nullptr;

  std::unique_ptr<ui::SimpleMenuModel::Delegate> context_menu_delegate_;
  std::unique_ptr<ui::SimpleMenuModel> context_menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> filter_menu_model_;
  std::unique_ptr<views::MenuRunner> context_menu_runner_;

  base::ScopedObservation<LiveFolderStore, LiveFolderStore::Observer>
      store_observation_{this};
  base::ScopedObservation<LiveFolderService, LiveFolderService::Observer>
      service_observation_{this};

  // Set between a scheduled rebuild and its execution, so a burst of store
  // notifications collapses into one.
  bool rebuild_pending_ = false;

  base::WeakPtrFactory<AvoraLiveFoldersView> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LIVE_FOLDERS_VIEW_H_
