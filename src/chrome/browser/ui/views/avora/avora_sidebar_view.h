// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SIDEBAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SIDEBAR_VIEW_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "build/build_config.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class Profile;

namespace views {
class ImageButton;
class Label;
class Textfield;
}  // namespace views

// Callback invoked for nav-button presses: back, forward, reload.
using AvoraNavCallback = base::RepeatingClosure;

// Callback invoked when the user clicks an extension icon in the manage bubble.
using AvoraExtensionClickCallback =
    base::RepeatingCallback<void(const std::string&)>;

// Callback invoked when the user commits a URL or search query in the
// address chip textfield.  The string may be a URL or free-text query.
using AvoraAddressCommitCallback =
    base::RepeatingCallback<void(const std::u16string&)>;

// Avora's custom sidebar header that sits above Chromium's vertical tab strip
// region.  Contains nav buttons (back/forward/reload) and an address chip.
// Chromium's own top/bottom containers (collapse, tab search, new tab)
// remain visible below this header.
class AvoraSidebarView : public views::View,
                         public views::TextfieldController {
  METADATA_HEADER(AvoraSidebarView, views::View)

 public:
#if BUILDFLAG(IS_MAC)
  static constexpr int kNavRowHeight = 44;
#else
  static constexpr int kNavRowHeight = 40;
#endif
  static constexpr int kAddressChipRowHeight = 48;
  static constexpr int kHeaderHeight =
      kNavRowHeight + kAddressChipRowHeight;
  static constexpr int kSpacesBarHeight = 40;

  AvoraSidebarView(Profile* profile,
                   AvoraNavCallback back_cb,
                   AvoraNavCallback forward_cb,
                   AvoraNavCallback reload_cb,
                   AvoraExtensionClickCallback extension_click_cb,
                   AvoraNavCallback extensions_cb,
                   AvoraNavCallback webstore_cb,
                   AvoraNavCallback settings_cb,
                   AvoraAddressCommitCallback address_commit_cb);
  AvoraSidebarView(const AvoraSidebarView&) = delete;
  AvoraSidebarView& operator=(const AvoraSidebarView&) = delete;
  ~AvoraSidebarView() override;

  views::View* nav_row() { return nav_row_; }
  views::View* address_chip_row() { return address_chip_row_; }
  views::ImageButton* manage_button() { return manage_button_; }

  // Update the URL displayed in the address chip.
  void SetURL(const std::u16string& url_text);

  // Returns the SpaceManager created by this sidebar.  The BrowserView
  // can pass this to the AvoraSpacesBarView it owns to upgrade the bar
  // from legacy pill mode to full managed mode.
  avora::SpaceManager* space_manager() { return space_manager_.get(); }

  // Sibling views use this to build their own Avora stores rather than
  // borrowing ours, which they must not outlive.
  Profile* profile() { return profile_; }

  // Returns true if the point (in this view's coordinates) is in an area
  // that should act as a window caption (draggable title bar).
  bool IsPositionInWindowCaption(const gfx::Point& point) const;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;
  void OnAfterUserAction(views::Textfield* sender) override {}

 private:
  raw_ptr<views::View> nav_row_ = nullptr;
  raw_ptr<views::ImageButton> manage_button_ = nullptr;
  raw_ptr<views::ImageButton> back_button_ = nullptr;
  raw_ptr<views::ImageButton> forward_button_ = nullptr;
  raw_ptr<views::ImageButton> reload_button_ = nullptr;
  raw_ptr<views::View> address_chip_row_ = nullptr;
  raw_ptr<views::View> address_chip_bg_ = nullptr;
  raw_ptr<views::Label> address_label_ = nullptr;
  raw_ptr<views::Textfield> address_field_ = nullptr;

  raw_ptr<Profile> profile_;
  AvoraExtensionClickCallback extension_click_cb_;
  AvoraNavCallback extensions_cb_;
  AvoraNavCallback webstore_cb_;
  AvoraNavCallback settings_cb_;
  AvoraAddressCommitCallback address_commit_cb_;

  // The last URL set via SetURL(), used to pre-fill the textfield.
  std::u16string current_url_text_;

  // Show the manage bubble anchored to the manage button.
  void ShowManageBubble();

  // Show the inline textfield for URL editing.
  void BeginEditing();

  // Hide the textfield and revert to the label.
  void EndEditing();

  // Spaces support — owns the SpaceManager; the bar itself is placed by
  // BrowserView and can call space_manager() to upgrade to managed mode.
  std::unique_ptr<avora::SpaceManager> space_manager_;

  base::WeakPtrFactory<AvoraSidebarView> weak_ptr_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SIDEBAR_VIEW_H_
