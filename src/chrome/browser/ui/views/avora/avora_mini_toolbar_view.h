// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_MINI_TOOLBAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_MINI_TOOLBAR_VIEW_H_

#include <map>
#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class BrowserWindowInterface;
class Profile;

namespace views {
class ImageButton;
class Label;
class LabelButton;
class MenuRunner;
class Textfield;
}  // namespace views

namespace avora {

// The mini window's chrome: a nav row, an address bar, and a Space selector,
// in place of the location bar a popup window would otherwise carry.
//
// This is a view and nothing more.  It does not own the page and is not its
// delegate -- the mini window's tab belongs to a TabStripModel and the Browser
// is its WebContentsDelegate, which is what gives the window native dialogs,
// permission prompts and extension plumbing.  The toolbar only watches: it
// observes the window's tab strip to follow the active tab, and that tab's
// WebContents to keep the address and the nav buttons honest.  Everything it
// does to the page it does through browser commands (IDC_BACK and friends),
// the same way the sidebar does in the main window.
class AvoraMiniToolbarView : public views::View,
                             public views::TextfieldController,
                             public TabStripModelObserver,
                             public content::WebContentsObserver,
                             public extensions::ExtensionRegistryObserver {
  METADATA_HEADER(AvoraMiniToolbarView, views::View)

 public:
  // Toolbar metrics.  The window's own geometry lives with the mini window
  // (see kMiniWindowMinWidth and friends in avora_mini_window.h).
  static constexpr int kToolbarHeight = 48;
  static constexpr int kToolbarHPadding = 10;
  static constexpr int kToolbarItemGap = 6;
  static constexpr int kNavButtonSize = 30;
  static constexpr int kNavIconSize = 18;
  static constexpr int kChipHeight = 32;
  static constexpr int kChipCornerRadius = 10;
  static constexpr int kSpaceButtonMaxWidth = 160;
  static constexpr int kSpaceIconSize = 16;
  static constexpr int kSpaceMenuButtonWidth = 22;
  static constexpr int kSpaceMenuIconSize = 14;
  static constexpr int kExtensionButtonSize = 26;
  static constexpr int kExtensionIconSize = 18;
  static constexpr int kExtensionGap = 2;

  // Run with the text the user committed in the address chip.  BrowserView
  // resolves and navigates it exactly as it does for the main window's address
  // bar, so a search term typed here behaves the same in both.
  using NavigateAddressCallback =
      base::RepeatingCallback<void(const std::u16string& text)>;

  AvoraMiniToolbarView(BrowserWindowInterface* browser,
                       NavigateAddressCallback navigate_address_cb);
  AvoraMiniToolbarView(const AvoraMiniToolbarView&) = delete;
  AvoraMiniToolbarView& operator=(const AvoraMiniToolbarView&) = delete;
  ~AvoraMiniToolbarView() override;

  // Brings the address chip and the Space chip back in line with the page and
  // the window's Space.  Called on its own when something outside the page
  // changes them; navigation drives it from the observers below.
  void UpdateState();

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // extensions::ExtensionRegistryObserver:
  void OnExtensionLoaded(content::BrowserContext* browser_context,
                         const extensions::Extension* extension) override;
  void OnExtensionUnloaded(content::BrowserContext* browser_context,
                           const extensions::Extension* extension,
                           extensions::UnloadedExtensionReason reason) override;

  // The button carrying |extension_id|'s icon, or null when this toolbar shows
  // no icon for it.  This is what an extension popup is anchored to, so that a
  // popup opened in the mini window hangs from the mini window's own chrome
  // rather than from anything in the main window.
  views::View* ExtensionAnchor(const std::string& extension_id);

  // The row itself, which is the fallback anchor.
  views::View* extensions_row() { return extensions_row_; }

  // content::WebContentsObserver:
  void DidStartLoading() override;
  void DidStopLoading() override;
  void NavigationEntryCommitted(
      const content::LoadCommittedDetails& load_details) override;

  // The address chip shows the URL as a label until clicked, then becomes a
  // textfield.  Public because the chip surface and the tests both drive it.
  void BeginEditingAddress();
  void EndEditingAddress();

  views::ImageButton* back_button_for_testing() { return back_button_; }
  views::ImageButton* forward_button_for_testing() { return forward_button_; }
  views::ImageButton* reload_button_for_testing() { return reload_button_; }
  views::Label* address_label_for_testing() { return address_label_; }
  views::Textfield* address_field_for_testing() { return address_field_; }
  views::LabelButton* space_button_for_testing() { return space_button_; }
  views::ImageButton* space_menu_button_for_testing() {
    return space_menu_button_;
  }

 private:
  void BuildUI();

  // Nav row.  Routed through browser commands rather than the page's
  // NavigationController, so back in the mini window means what back means
  // everywhere else in Avora.
  void OnBackPressed();
  void OnForwardPressed();
  void OnReloadPressed();
  void UpdateNavButtons();

  void UpdateAddress();

  // Extensions row.  One button per enabled extension that has an action: the
  // mini window's affordance for reaching an extension, and the anchor its
  // popup hangs from.
  void UpdateExtensionIcons();
  void OnExtensionIconPressed(const std::string& extension_id);

  // Space selector.  The picker commits to the Space it is showing; the
  // dropdown beside it is how the user aims somewhere else.
  void OnSpaceButtonPressed();
  void ShowSpaceMenu();
  void UpdateSpaceButton();

  // The window's tab, or null while the window has none.
  content::WebContents* active_contents() const;

  // Follows the active tab: the toolbar reports on whichever page the window
  // is showing.
  void ObserveActiveContents();

  Profile* profile() const;

  const raw_ptr<BrowserWindowInterface> browser_;
  NavigateAddressCallback navigate_address_cb_;

  raw_ptr<views::ImageButton> back_button_ = nullptr;
  raw_ptr<views::ImageButton> forward_button_ = nullptr;
  raw_ptr<views::ImageButton> reload_button_ = nullptr;
  raw_ptr<views::View> address_chip_ = nullptr;
  raw_ptr<views::Label> address_label_ = nullptr;
  raw_ptr<views::Textfield> address_field_ = nullptr;
  raw_ptr<views::View> extensions_row_ = nullptr;
  std::map<std::string, raw_ptr<views::ImageButton>> extension_buttons_;
  raw_ptr<views::View> space_chip_ = nullptr;
  raw_ptr<views::LabelButton> space_button_ = nullptr;
  raw_ptr<views::ImageButton> space_menu_button_ = nullptr;

  std::unique_ptr<ui::SimpleMenuModel> space_menu_model_;
  std::unique_ptr<ui::SimpleMenuModel::Delegate> space_menu_delegate_;
  std::unique_ptr<views::MenuRunner> space_menu_runner_;

  base::ScopedObservation<extensions::ExtensionRegistry,
                         extensions::ExtensionRegistryObserver>
      registry_observation_{this};

  base::WeakPtrFactory<AvoraMiniToolbarView> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_MINI_TOOLBAR_VIEW_H_
