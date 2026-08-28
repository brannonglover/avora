# Unnamed Browser

A small Chromium-based browser prototype built with [Chromium Embedded Framework (CEF)](https://bitbucket.org/chromiumembedded/cef/wiki/Home), with vertical tabs and a top address bar implemented in the native CEF client.

## Run it

```sh
npm run configure
npm run build
npm run start
```

`configure` downloads the CEF binary distribution (large, one-time setup). `build` compiles the Avora CEF target; `start` opens the built `Avora.app` on macOS.

For the upstream minimal sample instead:

```sh
npm run configure
npm run build:minimal
npm run start:minimal
```

Local npm cache (optional): install with `npm install --cache .npm-cache` if you want caches kept inside the repo; this project otherwise has no Node runtime dependencies for the browser itself.

## Current features

Features live in the CEF/native client under `native/cef-project` (Avora example). Chromium renders pages natively with full embedding control.

## Extensions

Chrome extension support depends on how much of the Chromium extension stack is wired in the CEF build. Compatibility and install flows are tracked in `docs/` and `native/README.md`.

## Next good steps

- Add pinned tabs and tab reordering
- Add a simple start page
- Add profile/session controls
- Add history and bookmarks
- Chrome Web Store CRX download/unpack/install where supported
- Native messaging, WebAuthn/passkeys, and extension compatibility reporting

## Architecture

- `native/cef-project`: official CEF baseline, CMake build, Avora/minimal targets
- `native/scripts`: configure, build, and open helpers
- `docs/`: migration notes and compatibility
