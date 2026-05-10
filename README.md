# Unnamed Browser

A small Chromium-based browser prototype with vertical tabs and a top address bar.

## Why Electron for the first build?

Electron is the fastest practical way to start because it ships Chromium and gives us direct control over browser surfaces. A lower-level CEF app can be leaner later, but it takes much longer to bootstrap correctly. This version keeps the UI simple and avoids a frontend build system so iteration stays quick.

## Run it

```sh
electron_config_cache="$PWD/.electron-cache" npm install --cache .npm-cache
npm run dev
```

The explicit cache settings keep Electron and npm downloads inside this project. That avoids issues on machines where the user-level npm or Electron cache has old permissions.

## Current features

- Chromium page rendering through Electron `WebContentsView`
- Vertical tab list
- Top address/search bar
- Back, forward, reload, new tab, close tab
- Keyboard shortcuts: `Cmd/Ctrl+L`, `Cmd/Ctrl+T`, `Cmd/Ctrl+W`
- Unpacked Chromium extension loading from the toolbar

## Extensions

Click the diamond button beside the new-tab button and paste either a Chrome Web Store URL or a 32-character extension ID. You can also type `1password` to install 1Password directly. The browser downloads the CRX package through Google's Chromium update service, shows progress in the lower-left corner, unpacks it, and loads it into the current browser session.

Leave the prompt blank to choose an unpacked Chrome extension folder manually. The folder must contain `manifest.json`; if you are using a local Chrome profile extension, select the version folder inside that extension's ID folder.

Electron does not support every Chrome extension API, so some downloaded extensions may install but not fully work yet. Store-downloaded extensions are currently unpacked under the app's user data directory and need persistence/auto-reload hardening next.

## Next good steps

- Add pinned tabs and tab reordering
- Add a simple start page
- Add profile/session controls
- Add history and bookmarks
- Persist loaded extension paths and reload them on app start
- Add extension compatibility warnings for unsupported Chrome APIs
- Evaluate CEF or another native shell if minimum memory use becomes the main goal

## Serious browser direction

The current app is an Electron prototype because it is the quickest way to build around Chromium. If this grows into a daily-driver browser, the natural path is:

1. Keep product UX, tab model, shortcuts, history, bookmarks, spaces, and command features in portable modules.
2. Use Electron while product ideas are changing quickly.
3. Move the engine host to CEF/native code only when the feature set is stable enough to justify the extra build complexity.
