# Avora for iOS & Android

The mobile companion to the Avora desktop browser (`~/projects/avora`), built with Expo. For now it's a plain browser: one WebView per tab, an address bar at the bottom, and tab management modeled on the desktop sidebar's **Pinned** / **Today** split.

## Run it

The app uses `react-native-webview`, so it needs a development build (Expo Go won't do):

```sh
npx expo run:ios       # or: npx expo run:android
```

After the first build, `npx expo start` is enough for JS changes.

Tab sync needs a Supabase project. Copy `.env.example` to `.env.local` and fill
it in; [docs/SYNC.md](docs/SYNC.md) covers setting up the project. Without it,
the browser works as usual and the sign-in UI stays hidden.

## Signing in and synced tabs

The person icon in the tab switcher opens **Account**. Sign in with your email:
tap the link in the email on this phone, or type the code from it. Once you're
signed in, every other device on your account gets its own section below
**Today**, with its tabs (pinned first). Tap one to open it here; if it's already open,
you're taken to that tab instead.

## How tabs work

| Gesture / control | What it does |
| --- | --- |
| Tap the address bar | Edit the URL or search. Typing also shows any open tabs that match, under "Switch to tab". |
| Swipe the address bar left / right | Go to the next / previous tab. Swiping left past the last tab opens a new one. |
| Swipe the address bar up, or tap the tab count | Open the tab switcher. |
| Long-press the tab count | Quick menu: New Tab, Reopen Closed Tab, Clear Today, Close This Tab. |
| `+` | New tab; the keyboard comes up ready to type. |
| `⋯` | Share, Pin/Unpin, Duplicate, Copy Link, Reopen Closed Tab, Close Tab. |

In the **tab switcher**:

- **Pinned** tabs sit in a grid at the top. They survive "Clear".
- **Today** is a list of everything else. Swipe a row left (or tap ✕) to close it, long-press for more actions, and use **Clear** to close all of them at once.
- Tabs that a page opens (`target="_blank"`) land right after the page that opened them. Closing one, or pressing Android's back button on it, takes you back to that page.

Closing a tab shows an **Undo** toast. Recently closed tabs are also listed on the start page and in the `⋯` menu. Tabs persist across launches; the five most recently used keep their page loaded, and older ones reload when you come back to them.

## Layout

```
src/app/            Expo Router routes: the browser (index) and the account sheet
src/browser/
  BrowserScreen.tsx  Screen shell: WebViews, the address-bar suggestions, menus
  BottomBar.tsx      Address bar + toolbar; swipe gestures for switching tabs
  TabSwitcher.tsx    Pinned grid + Today list
  tabs.ts            Tab state (reducer), persisted to AsyncStorage
  url.ts             Address-bar input → URL / Google search
  theme.ts           Colors from the desktop app
  SyncedTabs.tsx     Other devices' tabs, shown in the tab switcher
src/sync/
  supabase.ts        Supabase client; session encrypted at rest
  auth.tsx           Session context, magic link + code sign-in
  devices.ts         Other devices' tabs over Realtime
  AccountScreen.tsx  Sign in / signed-in sheet
supabase/migrations/ The devices table and its row-level security
```
