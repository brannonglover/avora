# Native Browser Foundation

This folder is the start of the CEF-based version of the browser.

The current Electron app remains useful as a fast UX prototype. The CEF app is the long-term browser engine path for stronger Chromium compatibility, native passkeys, and deeper extension support.

## Current Native Baseline

`native/cef-project` is the official Chromium Embedded Framework sample project. It can download a CEF binary distribution and build the minimal CEF browser sample.

On this machine:

- CPU: Apple Silicon / `arm64`
- Build system: CMake + Unix Makefiles using Apple Clang
- CEF project source: `native/cef-project`

## Commands

```sh
npm run cef:configure
npm run cef:build
npm run cef:open
```

The configure step downloads a CEF binary distribution from the Spotify CEF automated builder. It can take a while and uses significant disk space.

This repo uses the Makefiles generator because CMake's Xcode generator failed compiler detection with the currently installed Xcode 26.3 toolchain. Python 3.11 is preferred for the CEF helper scripts.

## Why This Step First

CEF applications have a native bundle layout, helper processes, framework resources, and macOS-specific runtime requirements. Getting the official minimal app building first gives us a known-good base before we move the browser UI over.

## Migration Order

1. Build and run the official CEF minimal sample.
2. Create a `avora` CEF target copied from the minimal sample.
3. Add browser lifecycle, address navigation, and basic tabs.
4. Rebuild the vertical sidebar UI natively or as a controlled local web UI.
5. Add Chrome Web Store download/unpack/install.
6. Add native messaging, passkey/WebAuthn, permissions, and extension compatibility checks.
