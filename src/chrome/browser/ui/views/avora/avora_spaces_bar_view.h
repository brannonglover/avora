// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SPACES_BAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SPACES_BAR_VIEW_H_

#include <map>
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
#include "ui/views/view.h"

class Profile;

namespace avora {
struct SpaceEditorFields;
}  // namespace avora

// A panel at the bottom of the sidebar that displays Space controls.
//
// In managed mode the bar is a centred strip of Lucide Space icons with a "+"
// pinned to the right edge.  Each icon is drawn in its Space's accent colour;
// name and icon are edited through the Space editor bubble rather than inline.
//
// Supports two modes:
//   1. Legacy mode  – constructed with just a callback; call SetSpaces()
//      to feed it data.  This keeps the existing BrowserView integration
//      compiling.
//   2. Managed mode – call ConnectToProfile() to hand it a Profile.
//      The view then self-updates via the observer interface.
class AvoraSpacesBarView : public views::View,
                           public avora::SpaceManagerObserver,
                           public avora::WindowSpaceState::Observer,
                           public avora::BrowserProfileStore::Observer {
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

 private:
  void TryAutoConnectSpaceManager();
  void RebuildLegacy();
  void RebuildManaged();

  void OnSpaceClicked(const std::string& id);
  void OnSpaceContextMenu(const std::string& id, const gfx::Point& screen_point);
  void OnCreateSpaceClicked();

  void ShowSpaceContextMenu(const std::string& space_id,
                            const gfx::Point& screen_point);

  // Space editor bubble, in its create and edit flavours.
  void ShowCreateSpaceEditor();
  void ShowEditSpaceEditor(const std::string& space_id);
  void OnCreateSpaceCommitted(const avora::SpaceEditorFields& fields);
  void OnEditSpaceCommitted(const std::string& space_id,
                            const avora::SpaceEditorFields& fields);

  void ConfirmDeleteSpace(const std::string& space_id);
  void DeleteSpace(const std::string& space_id);

  // The Space this window is showing, or nullptr when there is none.
  const avora::Space* GetActiveSpace() const;

  std::u16string ProfileDisplayName(const std::string& profile_id) const;

  // Tooltip line naming the Space's browser identity and saying whether that
  // identity has its own cookie jar, so the user can tell an isolated Space
  // apart from one sharing the default session.
  std::u16string IdentityTooltipLine(const std::string& profile_id) const;

  SpaceClickedCallback clicked_cb_;
  std::vector<SpaceInfo> legacy_spaces_;

  raw_ptr<Profile> profile_ = nullptr;
  std::unique_ptr<avora::SpaceManager> space_manager_;
  std::unique_ptr<avora::BrowserProfileStore> profile_store_;
  raw_ptr<avora::WindowSpaceState> window_space_state_ = nullptr;

  // Anchors the Space editor bubble; the "+" button in create mode, the
  // Space's own icon when editing.
  std::map<std::string, raw_ptr<views::View>> space_buttons_;
  raw_ptr<views::View> create_button_ = nullptr;

  std::unique_ptr<ui::SimpleMenuModel> context_menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> profile_submenu_model_;
  std::unique_ptr<ui::SimpleMenuModel::Delegate> context_menu_delegate_;
  std::unique_ptr<ui::SimpleMenuModel::Delegate> profile_submenu_delegate_;
  std::unique_ptr<views::MenuRunner> menu_runner_;

  base::WeakPtrFactory<AvoraSpacesBarView> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SPACES_BAR_VIEW_H_
