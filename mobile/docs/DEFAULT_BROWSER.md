# Avora as the default browser on iPhone

iOS lists an app under Settings → Apps → Default Browser App only when it is
signed with Apple's managed `com.apple.developer.web-browser` entitlement.
Apple grants it per bundle id on request. It was granted for
`com.avora.browser` on 2026-11-18.

Apple's page: https://developer.apple.com/documentation/xcode/preparing-your-app-to-be-the-default-browser

## What the app does

- `app.config.js` declares `http` and `https` under `ios.scheme` (they land in
  `CFBundleURLTypes`) and adds the entitlement under `ios.entitlements`.
  Prebuild writes it into `ios/Avora/Avora.entitlements`.
- iOS hands the default browser every http(s) link, on a cold start and while
  running.
- `src/app/+native-intent.tsx` stops expo-router from reading
  `https://example.com/news` as a `/news` screen. It queues the link
  (`src/browser/incoming.ts`) and shows the browser, and `BrowserScreen` opens
  each queued link in a new tab in the active Space once the saved tabs have
  loaded.
- Magic-link sign-in only looks at `avora://account` links, so a page whose
  URL carries its own `?code=` isn't exchanged as a sign-in.

## Signing

The App ID needs the capability enabled (Certificates, Identifiers &
Profiles → Identifiers → `com.avora.browser` → Capabilities, under Additional
Capabilities), or provisioning profiles won't include the entitlement and the
build fails to sign. Profiles made before it was enabled don't have it either;
EAS replaces a profile that doesn't match the app's entitlements, and
`npx eas-cli@latest credentials` can regenerate one by hand.

## Checking it on a phone

Install a build, choose Avora under Settings → Apps → Default Browser App, and
tap a link in Notes or Messages: once with Avora closed and once with it open.
Each link should open in a new tab.

## Apple's requirements

These are the boxes on the entitlement request form. Keep them true.

| Requirement | Status |
| --- | --- |
| Declares the http and https schemes in `Info.plist` | `ios.scheme` |
| Doesn't use `UIWebView` | `react-native-webview` uses `WKWebView` |
| On launch, offers a URL field, search, or bookmarks | The start page's address bar |
| Doesn't claim Universal Links for specific domains | No associated domains |
| None of `NSPhotoLibraryUsageDescription`, `NSLocationAlways…`, `NSHomeKitUsageDescription`, `NSBluetoothAlwaysUsageDescription`, `NSHealthShare…`/`NSHealthUpdate…` | Only `NSFaceIDUsageDescription` (expo-secure-store); check the generated `Info.plist` when adding a plugin |
| Opens an http(s) link directly at that destination | A new tab loads the link as given |
