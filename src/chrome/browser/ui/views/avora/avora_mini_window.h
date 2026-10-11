// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_MINI_WINDOW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_MINI_WINDOW_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/avora/avora_space.h"
#include "content/public/browser/web_contents_delegate.h"
#include "ui/views/controls/webview/unhandled_keyboard_event_handler.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"
#include "url/gurl.h"

class Profile;

namespace views {
class ImageButton;
class Label;
class LabelButton;
class MenuRunner;
class Textfield;
class WebView;
}  // namespace views

namespace avora {

class AvoraToastView;

// Opens |url| in Avora's mini window, creating the window or reusing the one
// already open.  This is where links arriving from outside Avora land: a link
// clicked in Mail or Slack is a glance, not a commitment, so it gets a window
// of its own instead of a tab in the user's working set.
//
// Deliberately does not create a browser window.  A mini window can be the
// only Avora window on screen; a tabbed window is created only if the user
// commits the link to a Space and none exists to receive it.
void ShowMiniWindow(Profile* profile, const GURL& url);

// Whether a mini window is currently open.
bool IsMiniWindowShowing();

// Closes the mini window if one is open.  No-op otherwise.
void CloseMiniWindow();

// The Spaces the mini window offers, in the order it offers them: |preferred|
// first -- the Space of the window the user was last in, which is far and away
// the likeliest destination -- then the rest in Space order.  An empty or
// unknown |preferred| leaves the order untouched.
std::vector<Space> OrderMiniWindowSpaces(const std::vector<Space>& spaces,
                                         const std::string& preferred);

// Contents of the mini window: a nav row, an address bar, and a Space selector
// over a single page.
//
// The page is an ordinary WebContents that Avora owns outright -- it never
// enters a TabStripModel -- so this view is its delegate and draws every piece
// of chrome around it.  Committing the page to a Space does not hand this
// WebContents over: Spaces have their own storage partitions, and a page
// loaded in one identity cannot be re-parented into another.  The destination
// Space opens the URL fresh instead.
class MiniWindowView : public views::View,
                       public content::WebContentsDelegate,
                       public content::WebContentsObserver,
                       public views::TextfieldController {
  METADATA_HEADER(MiniWindowView, views::View)

 public:
  // Window geometry.  Big enough to actually read a page in -- a mini window
  // is a page without a tab strip, not a peephole.
  static constexpr int kDefaultWidth = 1200;
  static constexpr int kDefaultHeight = 800;
  static constexpr int kMinWidth = 480;
  static constexpr int kMinHeight = 400;

  // Toolbar metrics.
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

  MiniWindowView(Profile* profile, const GURL& url);
  MiniWindowView(const MiniWindowView&) = delete;
  MiniWindowView& operator=(const MiniWindowView&) = delete;
  ~MiniWindowView() override;

  // Points the window at |url|, replacing whatever it was showing.
  void NavigateTo(const GURL& url);

  // What the window is showing now -- the URL a Space would receive.
  GURL current_url() const;

  content::WebContents* web_contents_for_testing() { return contents_.get(); }

  // views::View:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // content::WebContentsDelegate:
  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)>
          navigation_handle_callback) override;
  content::WebContents* AddNewContents(
      content::WebContents* source,
      std::unique_ptr<content::WebContents> new_contents,
      const GURL& target_url,
      WindowOpenDisposition disposition,
      const blink::mojom::WindowFeatures& window_features,
      bool user_gesture,
      bool* was_blocked) override;
  bool HandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override;
  void NavigationStateChanged(content::WebContents* source,
                              content::InvalidateTypes changed_flags) override;
  void LoadingStateChanged(content::WebContents* source,
                           bool should_show_loading_ui) override;
  void CloseContents(content::WebContents* source) override;

  // content::WebContentsObserver:
  void DidStartLoading() override;
  void DidStopLoading() override;
  void TitleWasSet(content::NavigationEntry* entry) override;

 private:
  void BuildUI();

  // Nav row.
  void OnBackPressed();
  void OnForwardPressed();
  void OnReloadPressed();
  void UpdateNavButtons();

  // Address bar.  The URL shows as a label until clicked, then becomes a
  // textfield -- the same two-state chip the sidebar uses.
  void BeginEditingAddress();
  void EndEditingAddress();
  void UpdateAddress();
  void UpdateWindowTitle();

  // Copies the page's URL and confirms with the same toast the main window
  // shows, so Cmd-Shift-C means one thing across Avora.
  void CopyUrlToClipboard();
  void ShowToast(const std::u16string& message);

  // Space selector.  The picker commits to the Space it is showing; the
  // dropdown beside it is how the user aims somewhere else.
  void OnSpaceButtonPressed();
  void ShowSpaceMenu();
  void UpdateSpaceButton();
  // The Space a click would commit to when the user does not pick one: the
  // Space of the most recently active window, else the global active Space.
  std::string PreferredSpaceId() const;
  std::vector<Space> OrderedSpaces() const;
  // Opens current_url() as a new daily tab in |space_id| and closes this
  // window.  Creates a tabbed window only when there is none to receive it.
  void SendToSpace(const std::string& space_id);

  const raw_ptr<Profile> profile_;

  // The page.  Owned here rather than by the WebView so the mini window is
  // unambiguously its owner, matching how the link preview holds its own.
  std::unique_ptr<content::WebContents> contents_;

  raw_ptr<views::WebView> web_view_ = nullptr;
  raw_ptr<views::ImageButton> back_button_ = nullptr;
  raw_ptr<views::ImageButton> forward_button_ = nullptr;
  raw_ptr<views::ImageButton> reload_button_ = nullptr;
  raw_ptr<views::View> address_chip_ = nullptr;
  raw_ptr<views::Label> address_label_ = nullptr;
  raw_ptr<views::Textfield> address_field_ = nullptr;
  raw_ptr<AvoraToastView> toast_ = nullptr;
  raw_ptr<views::View> space_chip_ = nullptr;
  raw_ptr<views::LabelButton> space_button_ = nullptr;
  raw_ptr<views::ImageButton> space_menu_button_ = nullptr;

  std::unique_ptr<ui::SimpleMenuModel> space_menu_model_;
  std::unique_ptr<ui::SimpleMenuModel::Delegate> space_menu_delegate_;
  std::unique_ptr<views::MenuRunner> space_menu_runner_;

  // Sends key events the page did not consume back through the focus manager,
  // which is what lets a window accelerator fire while the page has focus.
  views::UnhandledKeyboardEventHandler unhandled_key_handler_;

  base::WeakPtrFactory<MiniWindowView> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_MINI_WINDOW_H_
