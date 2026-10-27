# Tab sync (desktop side)

Avora uploads its open tabs to the user's Avora account so the phone app can
show them. The contract with the phone (row shape, HTTP calls, debounce,
sign-out) is `docs/SYNC.md` in the avoraapp repository; this page covers how
the desktop meets it.

```
TabSyncWatcher ──OnTabsChanged──▶ TabSyncService ──UploadScheduler──▶ SupabaseClient ──▶ public.devices
 (ui/views/avora)                  (avora/sync)     (3 s quiet, 10 s max)  (SimpleURLLoader)
```

## Signing in

Avora Settings (sidebar → Avora Settings, or `chrome://avora-settings`) has an
**Account** section. The user enters an email; `POST /auth/v1/otp` sends a
code (and a magic link, which the desktop ignores). They type the code;
`POST /auth/v1/verify` with `type: "email"` returns the session. The page sends
the email and code to the browser process and gets back only a status: it
never sees a token.

## The session

`avora::sync::SessionStore` encrypts the session (access token, refresh token,
expiry, user) with OSCryptAsync into the profile pref `avora.sync.session`; on
macOS that key lives in the "Avora Safe Storage" Keychain item, as for Style
Studio AI's key. Under `--use-mock-keychain`, or if secure storage is
unavailable, the session is kept in memory only and the page says the user
will have to sign in again after quitting.

The access token is refreshed with `POST /auth/v1/token?grant_type=refresh_token`
when it has less than a minute left, and once more if PostgREST answers 401. A
refresh token the server refuses signs the user out ("session ended").

## The device row

- `id`: a UUID generated once into `avora.sync.device_id` and kept across
  sign-out. If a sign-out could not delete the row (offline), or an upsert is
  refused by row-level security because the id belongs to another account, the
  install takes a new id so it never fights over a row it cannot write.
- `name`: the Mac's Computer Name (Sharing settings), read off the UI thread.
- `platform`: `macos`.
- `tabs`: every tab of every tabbed window of the profile, across Spaces.
  `chrome://`, `file://`, `about:` and other non-http(s) pages, link previews,
  popups (including the mini window) and incognito windows are left out.
  Usernames and passwords in URLs are stripped. A Space's daily tabs are copied
  into each of its windows; copies share an Avora tab GUID and are sent once,
  using the most recently active copy. `pinned` is a natively pinned tab
  (Favorites included) or the tab behind a sidebar pinned item; `space` is the
  Space's display name; `lastActiveAt` comes from
  `WebContents::GetLastActiveTime()`.

## Uploading

`TabSyncWatcher` observes every tabbed window's `TabStripModel` (tabs added,
closed, moved, navigated or retitled, pinned, the active tab changing) and
window activation. Each change marks the snapshot dirty; `UploadScheduler`
uploads after 3 s without changes, or 10 s after the first unsent change. The
snapshot is taken when the upload starts, an unchanged snapshot is not sent
again, only one upload is in flight, and failures back off from 15 s to 10 min.
Nothing is uploaded while the browser is quitting, so closing every window on
the way out does not empty the phone's list.

## Signing out

Sign-out stops new uploads, waits for any upsert already in flight, deletes the
row with `DELETE /rest/v1/devices?id=eq.{id}`, revokes the refresh token with
`POST /auth/v1/logout?scope=local` (best effort), and clears the stored session.

## Tests

```sh
autoninja -C out/Default avora_sync_unit_tests avora_browser_tests
out/Default/avora_sync_unit_tests
out/Default/avora_browser_tests --gtest_filter='AvoraTabSync*'
```

The unit tests cover the payload builder, the debounce, the encrypted store and
the exact HTTP requests. The browser tests sign in through the real settings
page against a fake Supabase (URLLoaderInterceptor) and check the uploads and
the delete.
