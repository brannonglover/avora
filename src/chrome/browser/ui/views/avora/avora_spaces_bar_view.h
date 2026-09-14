// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SPACES_BAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SPACES_BAR_VIEW_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/avora/avora_profile.h"
#include "chrome/browser/avora/avora_space.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class Profile;

namespace views {
class Label;
class Textfield;
}  // namespace views

// A panel at the bottom of the sidebar that displays Space controls.
//
// Supports two modes:
//   1. Legacy mode  – constructed with just a callback; call SetSpaces()
//      to feed it data.  This keeps the existing BrowserView integration
//      compiling.
//   2. Managed mode – call ConnectToProfile() to hand it a Profile.
//      The view then self-updates via the observer interface and shows
//      icons, the active-space name, and a "+" button.
class AvoraSpacesBarView : public views::View,
                           public avora::SpaceManagerObserver,
                           public avora::WindowSpaceState::Observer,
                           public avora::BrowserProfileStore::Observer,
                           public views::TextfieldController {
  METADATA_HEADER(AvoraSpacesBarView, views::View)

 public:
  // Legacy data struct kept for backward compatibility with BrowserView.
  struct SpaceInfo {
    std::string id;
    std::string name;
    SkColor color = SK_ColorWHITE;
    bool is_active = false;
  };

  using SpaceClickedCallback =
      base::RepeatingCallback<void(const std::string&)>;

  // Legacy constructor (used by BrowserView's existing integration).
  explicit AvoraSpacesBarView(SpaceClickedCallback clicked_cb);

  AvoraSpacesBarView(const AvoraSpacesBarView&) = delete;
  AvoraSpacesBarView& operator=(const AvoraSpacesBarView&) = delete;
  ~AvoraSpacesBarView() override;

  // Legacy: set spaces from the outside (ignored once a SpaceManager is set).
  void SetSpaces(const std::vector<SpaceInfo>& spaces);

  // Upgrade to managed mode: the view creates its own SpaceManager and
  // BrowserProfileStore for |profile| and rebuilds itself automatically.
  void ConnectToProfile(Profile* profile);

  // Attach this bar to a per-window active-Space holder.  Once set, space
  // clicks and gesture switches operate on this window's state instead of
  // the global pref.  Must be called before or shortly after AddedToWidget().
  void SetWindowSpaceState(avora::WindowSpaceState* state);

  // views::View:
  void AddedToWidget() override;

  // avora::SpaceManagerObserver:
  void OnSpacesChanged() override;
  void OnActiveSpaceChanged(const std::string& space_id) override;

  // avora::WindowSpaceState::Observer:
  void OnWindowActiveSpaceChanged(const std::string& space_id) override;

  // avora::BrowserProfileStore::Observer:
  void OnBrowserProfilesChanged() override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  void TryAutoConnectSpaceManager();
  void RebuildLegacy();
  void RebuildManaged();

  void OnSpaceClicked(const std::string& id);
  void OnSpaceContextMenu(const std::string& id, const gfx::Point& screen_point);
  void OnCreateSpaceClicked(const gfx::Point& screen_point);
  void CreateSpaceWithIcon(const std::string& icon);

  void ShowSpaceContextMenu(const std::string& space_id,
                            const gfx::Point& screen_point);
  void ShowCreateSpaceMenu(const gfx::Point& screen_point);
  void ConfirmDeleteSpace(const std::string& space_id);
  void DeleteSpace(const std::string& space_id);

  void BeginRename();
  void CommitRename();
  void CancelRename();

  std::u16string ProfileDisplayName(const std::string& profile_id) const;

  SpaceClickedCallback clicked_cb_;
  std::vector<SpaceInfo> legacy_spaces_;

  raw_ptr<Profile> profile_ = nullptr;
  std::unique_ptr<avora::SpaceManager> space_manager_;
  std::unique_ptr<avora::BrowserProfileStore> profile_store_;
  raw_ptr<avora::WindowSpaceState> window_space_state_ = nullptr;

  raw_ptr<views::Label> active_name_label_ = nullptr;
  raw_ptr<views::Textfield> rename_field_ = nullptr;

  std::unique_ptr<ui::SimpleMenuModel> context_menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> icon_submenu_model_;
  std::unique_ptr<ui::SimpleMenuModel> profile_submenu_model_;
  std::unique_ptr<ui::SimpleMenuModel> create_icon_submenu_model_;
  std::unique_ptr<ui::SimpleMenuModel::Delegate> context_menu_delegate_;
  std::unique_ptr<ui::SimpleMenuModel::Delegate> icon_submenu_delegate_;
  std::unique_ptr<ui::SimpleMenuModel::Delegate> profile_submenu_delegate_;
  std::unique_ptr<ui::SimpleMenuModel::Delegate> create_icon_submenu_delegate_;
  std::unique_ptr<views::MenuRunner> menu_runner_;

  base::WeakPtrFactory<AvoraSpacesBarView> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SPACES_BAR_VIEW_H_
