# CEF Notes

The product is hosted on **Chromium Embedded Framework (CEF)** so we own native bundle layout, helper processes, macOS entitlements, and a path toward stronger Chromium extension APIs (background service workers, `chrome.action`, native messaging, passkeys/WebAuthn) than a lightweight embedding host typically provides.

## Sources

- CEF official docs: https://chromiumembedded.github.io/cef/
- CEF general usage: https://chromiumembedded.github.io/cef/general_usage.html
- Official `cef-project`: https://github.com/chromiumembedded/cef-project

## Commands

```sh
npm run configure
npm run build
npm run start
```

`configure` downloads the CEF binary distribution. Expect a large download.

On this machine, CMake's Xcode generator failed compiler detection with Xcode 26.3. The checked-in scripts use `Unix Makefiles`, explicit Apple Clang compiler paths, and Python 3.11 where available.

## Product Architecture

- `native/cef-project`: official CEF baseline and build system (including the Avora target)
- `native/scripts`: repo-level wrappers for configure / build / open
- `docs/`: architecture and compatibility notes

## CEF Work Items

1. Confirm CEF minimal builds and opens on macOS arm64.
2. Evolve the `Avora` CEF target beyond the minimal sample.
3. Add a browser client with address navigation.
4. Add a top toolbar and vertical tab list.
5. Add multiple `CefBrowser` instances or a browser-host manager for tabs.
6. Add Chrome Web Store CRX download/unpack/install where feasible.
7. Add extension compatibility reporting before install.
8. Implement native messaging and passkey support.
