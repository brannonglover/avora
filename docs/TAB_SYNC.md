# Shared tabs (desktop side)

An Avora account has one set of tabs. Every signed-in device, whether a Mac
or the phone, shows the same Spaces, Favorites, pinned items and today tabs,
and any of them can change them. The contract (tables, writes, reads,
previews) is `mobile/docs/SYNC.md`, along with the
migrations that create the tables. This page covers how the desktop meets it.

```
 AvoraSpaceTabFilter ─opened/navigated/closed─▶ TabSyncWatcher ─▶ TabSyncService
 (today tabs, per window) ◀─Upsert/Remove (posted)─┘                   │
 SpaceManager, SidebarItemStore ◀─────── SharedModels ◀────────────────┤
                                                                       │
                     SharedStore (mirror, links, unsent changes) ◀─────┤
                                                                       ▼
                    SupabaseClient ─▶ rpc/sync_items, rpc/sync_spaces, GET items/spaces,
                                      storage tab-previews, devices
```

## Signing in

Avora Settings (sidebar → Avora Settings, or `chrome://avora-settings`) has an
**Account** section. The user enters an email, and `POST /auth/v1/otp` sends a
code. They type the code, and `POST /auth/v1/verify` with `type: "email"`
returns the session. The page gets back only a status, never a token. The
session is encrypted with OSCryptAsync into `avora.sync.session` (Keychain on
macOS), and refreshed a minute before it expires and once more if the server
answers 401. Under `--use-mock-keychain` it lives in memory only.

## What is shared

- **Spaces** (`SpaceManager`): name, icon, colour, theme, order. Cookie jars are
  per device.
- **Favorites and pinned items** (`SidebarItemStore` kFavorite and kPinned).
  Pinned folders aren't shared yet: items sync flat, and an item filed in a
  folder here stays filed.
- **Today tabs**: a Space's daily tabs, which this Mac already keeps as one
  list across its windows (copies share an Avora tab GUID). The GUID is the
  shared row's id.

What stays on each device: the active tab, windows, history, the loaded
page, incognito windows, `chrome://` and other local pages, and recently
closed tabs.

## How changes move

- **This Mac's changes.** Spaces, Favorites and pinned items are diffed from
  their models (`SharedModels` projects them, and `SharedStore::Reconcile`
  compares against the last baseline, so only this device's own edits are
  sent, column by column). Today tabs are reported by the tab filter's
  `FlushDailyTabSync()`, the same place they are carried between windows.
  Opened, moved to another Space, navigated and closed by the user are
  reported; a window closing or Avora quitting is not, so neither ever closes
  tabs elsewhere. Writes go up through `rpc/sync_items` and `sync_spaces` a
  second after the change.
- **Other devices' changes.** Fetched every 10 seconds, and at once when a
  window comes to the front: rows with `updated_at` after the cursor, minus a
  minute of overlap. Unsent local changes win until they are sent.
  - Spaces, Favorites and pinned items are written into their models.
  - A today tab opens **unloaded** (one restored entry, no renderer) in each
    window that shows its Space.
  - A navigation replaces an unloaded copy. The copy on screen is never
    navigated: it follows once the user leaves it, and not at all if they
    navigate it first.
  - A close closes every copy.
  - A window entering a Space for the first time also gets that Space's
    shared tabs that no window here has open.
- **First sign-in** merges rather than replaces. Local Spaces link to account
  Spaces of the same name, and items and open tabs to rows of the same kind
  and URL in that Space. Everything else is uploaded.
- **Sign-out** removes the account from this Mac (see below).

## Signing out

A Mac's Spaces and tabs belong to the account, so signing out takes them off
the Mac; they stay in the account and come back on the next sign-in. Left
behind, they would merge into whichever account signed in next.

1. Settings asks first: the other windows and tabs will close.
2. Unsent changes go up. If they can't, the Mac stays signed in with
   `kUnsyncedChanges`: Settings says how many would be lost and offers **Sign
   out anyway**, which skips this step.
3. The `devices` row is deleted and the session revoked, as before.
4. `SharedModels::ClearForSignOut` removes every Space with its Favorites,
   pinned items and pinned folders, and leaves one new "Default Space" with a
   new identity. Then `TabSyncWatcher::ClearForSignOut` keeps the new tab the
   Space tab filter opens for that Space in the front window, closes that
   window's other tabs and every other window, and clears recently closed
   tabs. Order matters: each window holds copies of the same tabs and
   closing a copy closes it everywhere, so the other windows close whole,
   and the front tab never moves, so the filter opens no more new tabs.
5. The shared rows are forgotten.

Each Space owns an identity (its cookie jar, so its website sign-ins).
Removing a Space normally retires it. On sign-out each identity is instead
**parked** in `avora.sync.parked_identities` under the account's user id and
the Space's shared id. When that account signs in here again, a Space created
for that shared id (or linked to it by name, like the new "Default Space")
takes the parked identity back, so its sites are still signed in. Another
account never gets them. Parked identities' data stays on disk until the
account signs in again; no UI shows it.

`avora.sync.local_owner` records whose Spaces and tabs are on the Mac. An
expired session keeps them, so nothing unsent is lost, and the same account
signing in again merges as before. A different account signing in clears
them first, parking their identities for the old account.

History and other local data stay on the Mac.

## Keeping tabs quick

- **Previews**: a moment after a window's front tab finishes loading, Avora
  takes a JPEG of it and uploads it to `tab-previews/{user}/{id}.jpg`. It is
  at most 640 px wide and 256 KB, and taken at most once a minute per tab.
  Pages with a password field (checked in an isolated world) or served with
  `Cache-Control: no-store` get none, and lose any they had. The Account
  section has a switch; turning it off deletes the account's previews.
- **Warm budget**: each window keeps at most 12 background daily tabs loaded
  and unloads the least recently used of the rest, keeping their history. A
  tab playing audio, being captured or asking before it unloads stays loaded.
- Not done yet on the desktop: showing a preview over an unloaded tab while it
  loads (the phone does). On macOS the web contents are a native view, so it
  needs an overlay window.

## Tests

```sh
autoninja -C out/Default avora_sync_unit_tests avora_browser_tests
out/Default/avora_sync_unit_tests
out/Default/avora_browser_tests --gtest_filter='AvoraTabSync*:AvoraDailyTabSync*'
```

- **Unit tests**: order keys (checked against the npm package's own
  examples), row parsing, and `SharedStore` (diffs, echoes, unsent changes,
  links, persistence).
- **Browser tests**: run against a fake Supabase that keeps rows the way
  `sync_items` does. They cover sign-in and merge, local changes going up,
  remote opens, navigations, closes and pins coming down, previews, the warm
  budget, sign-out (clearing, parked identities coming back, another
  account getting none, and unsent changes asking first).
