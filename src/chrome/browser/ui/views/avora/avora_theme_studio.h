// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_THEME_STUDIO_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_THEME_STUDIO_H_

#include <string>

class PrefService;

namespace views {
class View;
}

namespace avora {

// Opens the Theme Studio for |space_id| in a bubble anchored to |anchor_view|,
// which must be in the browser window showing that Space.
//
// The user drags colour orbs across a pad, or picks a mood, and the window
// repaints as they go.  Nothing is saved until the bubble closes: Done or
// clicking away keeps the theme (and syncs it with the Space), Cancel or Esc
// puts the window back as it was.
void ShowThemeStudioBubble(views::View* anchor_view,
                           PrefService* prefs,
                           const std::string& space_id);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_THEME_STUDIO_H_
