# Space themes

Each Space can have its own theme: one or two colours and light or dark. The
window showing that Space paints its sidebar, frame and tabs in the theme, and
the theme syncs with the Space, so the phone shows it too. Chromium's own
"Pick a theme color" and "Pick an avatar" are gone from Settings, and so is the
Chrome Web Store Themes row.

```
 Theme Studio ──SetPreview──▶ WindowTheme (one per window) ──ThemeChanged──▶ views
      │                          │   ▲                                      │
      │ SetSpaceTheme on close   │   └─ active Space's theme                │ PaletteFor(view)
      ▼                          ▼                                          ▼
 SpaceManager ──▶ SharedModels   BrowserWidget::GetColorProviderKey     SpaceThemePalette
 (Space.theme)    (spaces.theme)   key.app_controller = theme supplier ─▶ Chromium's mixers
```

## The theme

`avora::SpaceTheme` (`chrome/browser/avora/avora_space_theme.h`) holds the
picked colours, the mode and, when the theme started as one, the mood id.
It is stored on the Space as its canonical JSON (`Serialize()`), which is also
the `spaces.theme` column. The format is part of the sync contract: see
*Space themes* in `docs/SYNC.md` in the avoraapp repository. An empty theme
keeps Avora's default graphite look, so Spaces made before themes look as they
always did.

`PaletteForSpaceTheme()` turns a theme into every colour the chrome uses:
the sidebar (top and bottom, for gradients), popover surfaces, raised chips,
the tab states, text and washes. The picked colours are vivid; the chrome takes
their hue at a fixed lightness per mode and well under half their saturation,
and text is nudged until it clears 7:1 against the sidebar and 4.5:1 against
everything set into it. `DefaultSpaceThemePalette()` is the old hardcoded
palette, colour for colour.

## Painting a window

`avora::WindowTheme` (`chrome/browser/ui/views/avora/avora_window_theme.h`)
is made by BrowserView once it is in its widget and goes with the widget. It
follows the window's active Space, or a preview while the Theme Studio is
open, and calls `Widget::ThemeChanged()` when the theme changes.

- Avora's own views read `WindowTheme::PaletteFor(this)` when they paint or in
  `OnThemeChanged()`. Bubbles find the palette through their anchor widget.
  The sidebar header, tab strip and spaces bar fill with
  `CreateSidebarBackground()`, which paints the slice of a window-tall
  gradient each one covers, so a two-colour theme runs without seams.
- Chromium's own surfaces (frame, toolbar, tab backgrounds, the page
  background) get the palette through the colour pipeline. `BrowserWidget`
  sets `ColorProviderKey::app_controller` to a supplier for the window's theme
  (web app windows keep their app's). The supplier's mixer runs after
  `AddAvoraColorMixer`, which paints the default palette.

A themed window also gets a colour-mode override (light or dark, from the
theme), and so does every bubble Avora opens from it
(`WindowTheme::MatchColorMode`), so what Chromium paints itself (dialog
titles and buttons, fields, the New Tab page) suits the palette. Views don't
repaint on a theme change by themselves, so `WindowTheme` invalidates the
window's whole view tree and its open bubbles after each change.

Chromium caches one colour provider per key forever, and keys tell suppliers
apart by address, so one supplier lives per distinct theme until
`ReleaseAvoraSpaceThemeSuppliers()`. The studio calls that when it closes,
then sends a native theme update so every window rebuilds what it needs.
While the studio is open it rounds pad colours to a 120×40 grid and repaints
at most every 50 ms, which bounds how many providers a drag leaves behind.

## Theme Studio

Right-click a Space → **Theme Studio…** (`avora_theme_studio.cc`). The window
switches to that Space if it isn't already showing it.

- **Colour pad**: hue across, vibrance up. Drag the orb; the window follows.
  **Blend two** adds a second orb, and the sidebar runs from the first colour
  at the top to the second at the bottom. Arrow keys move the selected orb and
  space switches between orbs.
- **Dark / Light** sets the mode.
- **Moods**: Dusk, Tide, Ember, Arcade, Graphite, Matcha, Paper, Blossom. Each
  card is a tiny sidebar in that mood; picking one glides the orbs there.
- **Reset** goes back to Avora's own look.

Done, or clicking away, keeps the theme. Cancel or Esc puts the window back.
Keeping writes the theme and sets the Space's accent to its first colour in
one `SpaceManager::SetSpaceTheme()` call, and sync sends both a second later.

## Sync

`spaces.theme` is a text column. A device sends it like any other column when
it changes. A row without the column (an older server, or a build from before
themes) leaves the local theme alone. An empty string clears the theme.
