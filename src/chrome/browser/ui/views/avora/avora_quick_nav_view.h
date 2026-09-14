// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_QUICK_NAV_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_QUICK_NAV_VIEW_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/timer/timer.h"
#include "chrome/browser/avora/avora_favorites.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/events/event.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class Browser;
class BrowserWindowInterface;

namespace avora {
class SearchSuggestProvider;
}  // namespace avora

namespace gfx {
class ImageSkia;
}

namespace views {
class ImageButton;
class ImageView;
class Textfield;
}  // namespace views

namespace avora {

// Callback invoked when the user commits a URL or search query.
// The bool parameter indicates whether to open in a new tab (CMD+T)
// or navigate the current tab (CMD+L).
using QuickNavCommitCallback =
    base::RepeatingCallback<void(const std::u16string&, bool /*new_tab*/)>;

// Callback invoked when the overlay should be dismissed.
using QuickNavDismissCallback = base::RepeatingClosure;

// Full-window overlay triggered by CMD+T.  Shows a centered search input
// field with a grid of favorite-site favicons (32 px) below it.  Pressing
// Enter navigates; pressing Escape or clicking the backdrop dismisses.
class AvoraQuickNavView : public views::View,
                          public views::TextfieldController,
                          public FavoritesManager::Observer {
  METADATA_HEADER(AvoraQuickNavView, views::View)

 public:
  // Input field dimensions.
  static constexpr int kInputWidth = 560;
  static constexpr int kInputHeight = 48;
  static constexpr int kInputCornerRadius = 12;

  // Favicon grid settings.
  static constexpr int kFaviconSize = 32;
  static constexpr int kGridIconPadding = 16;
  static constexpr int kGridTopMargin = 24;
  static constexpr int kGridMaxPerRow = 8;

  // Card (container holding input + grid) settings.
  static constexpr int kCardPaddingH = 16;
  static constexpr int kCardPaddingTop = 16;
  static constexpr int kCardPaddingBottom = 16;
  static constexpr int kCardCornerRadius = 16;
  static constexpr int kCardMaxWidth = 640;
  static constexpr int kCardTopOffset = 100;  // distance from top of window

  AvoraQuickNavView(BrowserWindowInterface* browser,
                    avora::WindowSpaceState* window_space_state,
                    QuickNavCommitCallback commit_cb,
                    QuickNavDismissCallback dismiss_cb);
  AvoraQuickNavView(const AvoraQuickNavView&) = delete;
  AvoraQuickNavView& operator=(const AvoraQuickNavView&) = delete;
  ~AvoraQuickNavView() override;

  // Show the overlay (CMD+T). Input is empty; current URL appears in list.
  void Show(const std::string& current_page_url);

  // Show with the given text pre-filled and selected (CMD+L).
  void ShowWithURL(const std::u16string& url_text,
                   const std::string& current_page_url);

  // Hide the overlay.
  void Hide();

  bool IsShowing() const;

  // views::View overrides:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;
  void VisibilityChanged(views::View* starting_from, bool is_visible) override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;

  // FavoritesManager::Observer:
  void OnFavoritesChanged() override;

 private:
  // Common implementation for Show() and ShowWithURL().
  void ShowOverlay(const std::u16string& url_text);

  // Build the centered card containing input + favorites grid.
  void BuildCard();

  // Rebuild the history/favorites list.
  void RebuildHistoryList();

  // Update the visual selection highlight.
  void UpdateSelection(int new_index);

  // Move selection by delta (-1 = up, +1 = down).
  void MoveSelection(int delta);

  // Position the card in the center-top of the overlay.
  void LayoutCard();

  // Load a favicon for a history row.
  void LoadFavicon(views::View* row_view, const std::string& url);

  // Load the favicon for the input field inline icon.
  void LoadInputFavicon(const std::string& url);

  // Called when a favorite icon is clicked.
  void OnFavoriteClicked(const std::string& url);

  // Fetch search suggestions for the current query (called after debounce).
  void FetchSuggestions(const std::string& query);

  // Callback from SearchSuggestProvider.
  void OnSuggestionsReceived(const std::string& query,
                             std::vector<std::string> suggestions);

  // Replace the list contents with search suggestions.
  void RebuildSuggestionList(const std::string& query,
                             const std::vector<std::string>& suggestions);

  raw_ptr<BrowserWindowInterface> browser_;
  QuickNavCommitCallback commit_cb_;
  QuickNavDismissCallback dismiss_cb_;

  // Backdrop child (has the blur layer).
  raw_ptr<views::View> backdrop_ = nullptr;

  // The floating card view.
  raw_ptr<views::View> card_ = nullptr;

  // The search input field and its inline favicon.
  raw_ptr<views::ImageView> input_favicon_ = nullptr;
  raw_ptr<views::Textfield> input_field_ = nullptr;

  // The container holding the history list.
  raw_ptr<views::View> grid_container_ = nullptr;

  // True when overlay was opened via CMD+T (navigate in new tab).
  bool open_in_new_tab_ = true;

  // Guard to prevent clearing selection when we programmatically update input.
  bool updating_from_selection_ = false;

  // The current page's URL (shown as first row, highlighted).
  std::string current_page_url_;

  // Keyboard selection index (-1 = none).
  int selected_index_ = -1;

  // URLs in display order, for keyboard navigation.
  std::vector<std::string> row_urls_;

  std::unique_ptr<FavoritesManager> favorites_manager_;
  raw_ptr<avora::WindowSpaceState> window_space_state_ = nullptr;

  // Search suggestion support.
  std::unique_ptr<SearchSuggestProvider> suggest_provider_;
  base::OneShotTimer suggest_timer_;
  bool showing_suggestions_ = false;

  base::CancelableTaskTracker cancelable_task_tracker_;
  base::WeakPtrFactory<AvoraQuickNavView> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_QUICK_NAV_VIEW_H_
