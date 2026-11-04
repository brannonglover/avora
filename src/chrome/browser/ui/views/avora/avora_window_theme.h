// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_WINDOW_THEME_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_WINDOW_THEME_H_

#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/scoped_observation.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_space_theme.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "ui/views/widget/widget_observer.h"

namespace views {
class View;
class Widget;
}  // namespace views

namespace avora {

// The theme one browser window is painted with: its active Space's, or while
// the Theme Studio is open, the one being made.
//
// One per browser window, made by BrowserView once it is in its widget and
// gone with the widget.  When the theme changes it re-themes
// the widget, so every view gets OnThemeChanged(); Avora's views read their
// colours from PaletteFor() there, and BrowserWidget hands Chromium's
// mixers the same theme through its ColorProviderKey.
class WindowTheme : public WindowSpaceState::Observer,
                    public SpaceManagerObserver,
                    public views::WidgetObserver {
 public:
  WindowTheme(const WindowTheme&) = delete;
  WindowTheme& operator=(const WindowTheme&) = delete;

  // Gives |widget| a theme that follows |space_state|, which must outlive
  // the widget's OnWidgetDestroying().
  static void Attach(WindowSpaceState* space_state, views::Widget* widget);

  // The window theme of |widget|, or of the browser window it opened from
  // (a bubble's anchor); null outside browser windows.
  static WindowTheme* FromWidget(const views::Widget* widget);

  // The palette |view| should paint with: its window's, or the default when
  // it is in no browser window or not in a widget yet.
  static const SpaceThemePalette& PaletteFor(const views::View* view);

  // Gives |bubble| the light or dark mode of the window it opened from, so
  // what Chromium paints in it -- titles, dialog buttons, fields -- suits the
  // palette.  Bubbles keep their own colour key, so this is set when each
  // opens; windows with no theme leave it to the system.
  static void MatchColorMode(views::Widget* bubble);

  const SpaceTheme& theme() const { return theme_; }
  const SpaceThemePalette& palette() const { return palette_; }

  // Paints the window with |theme| instead of its Space's until cleared with
  // std::nullopt.  Nothing is saved: the Theme Studio commits through
  // SpaceManager::SetSpaceTheme() when it closes.
  void SetPreview(std::optional<SpaceTheme> theme);
  bool previewing() const { return preview_.has_value(); }

  // The Space whose theme this window shows.
  const std::string& space_id() const;

  base::WeakPtr<WindowTheme> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  WindowTheme(WindowSpaceState* space_state, views::Widget* widget);
  ~WindowTheme() override;

  // WindowSpaceState::Observer:
  void OnWindowActiveSpaceChanged(const std::string& space_id) override;
  // SpaceManagerObserver:
  void OnSpacesChanged() override;
  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

  // The preview if there is one, else the active Space's theme.
  SpaceTheme CurrentTheme() const;

  // Re-themes the widget if CurrentTheme() changed.
  void Update();

  const raw_ptr<WindowSpaceState> space_state_;
  const raw_ptr<views::Widget> widget_;
  std::optional<SpaceTheme> preview_;
  SpaceTheme theme_;
  SpaceThemePalette palette_;

  base::ScopedObservation<WindowSpaceState, WindowSpaceState::Observer>
      space_state_observation_{this};
  base::ScopedObservation<SpaceManager, SpaceManagerObserver>
      space_manager_observation_{this};
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
  base::WeakPtrFactory<WindowTheme> weak_factory_{this};
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_WINDOW_THEME_H_
