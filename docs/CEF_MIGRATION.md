# CEF Migration Notes

We are moving toward CEF because Electron is useful for prototyping browser UI, but it does not provide full Chrome extension compatibility. In particular, extensions like 1Password depend on APIs such as Manifest V3 background service workers, `chrome.action`, `nativeMessaging`, and passkey-related native integration that Electron does not fully support.

CEF gives us a lower-level Chromium host. It still is not “Chrome in a box,” but it is the better foundation for a serious standalone browser because we can own:

- native bundle and helper process layout
- macOS entitlements and `Info.plist`
- WebAuthn/passkey behavior
- native messaging hosts
- extension install and compatibility flow
- browser profile/session storage
- permissions UI
- downloads and Web Store integration

## Sources

- CEF official docs: https://chromiumembedded.github.io/cef/
- CEF general usage: https://chromiumembedded.github.io/cef/general_usage.html
- Official `cef-project`: https://github.com/chromiumembedded/cef-project

## Immediate Goal

Build the official CEF minimal sample under `native/cef-project`, then copy that into a first-party `avora` target.

## Commands

```sh
npm run cef:configure
npm run cef:build
npm run cef:open
```

`cef:configure` downloads the CEF binary distribution. Expect a large download.

On this machine, CMake's Xcode generator failed compiler detection with Xcode 26.3. The checked-in scripts use `Unix Makefiles`, explicit Apple Clang compiler paths, and Python 3.11 where available.

## Product Architecture

The current Electron prototype should remain the UX lab. The native CEF browser should be the product foundation.

Suggested split:

- `src/`: Electron prototype
- `native/cef-project`: official CEF baseline and build system
- `native/avora`: future first-party browser target
- `docs/`: architecture and compatibility notes

## CEF Work Items

1. Confirm CEF minimal builds and opens on macOS arm64.
2. Create `native/avora` from the minimal sample.
3. Add a browser client with address navigation.
4. Add a top toolbar and vertical tab list.
5. Add multiple `CefBrowser` instances or a browser-host manager for tabs.
6. Add Chrome Web Store CRX download/unpack/install.
7. Add extension compatibility reporting before install.
8. Implement native messaging and passkey support.
