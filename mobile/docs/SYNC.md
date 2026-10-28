# Tab sync

An Avora account has one set of tabs. Every signed-in device, whether a Mac or
the phone, shows the same Spaces, Favorites, pinned items and today tabs, and
any of them can change them. Opening a tab on the phone opens it on every Mac;
closing it anywhere closes it everywhere.

```
Avora (Mac) ──changed rows──▶ public.spaces ◀──changed rows── Avora (phone)
            ◀──catch-up/live── public.items  ──catch-up/live──▶
            ──previews──▶ storage: tab-previews ◀──previews──
```

What each device keeps to itself: which tab is active, window layout,
back/forward history, scroll position, the live page, and recently closed
tabs (only the device that closed a tab can reopen it).

## Setting up the Supabase project

1. Create a project, then apply the migrations in `supabase/migrations/` in
   order (`supabase db push`, or paste each into the SQL editor).
2. **Authentication → Sign In / Providers**: leave Email enabled. Turning off
   "Confirm email" isn't needed, because a magic link both confirms and signs in.
3. **Authentication → URL Configuration → Redirect URLs**: add `avora://**`.
4. **Authentication → Emails → Magic Link**: add the one-time code so people
   can sign in when they open the email on a different device from the one
   that asked for it:

   ```html
   <h2>Sign in to Avora</h2>
   <p><a href="{{ .ConfirmationURL }}">Sign in</a></p>
   <p>Or enter this code: <strong>{{ .Token }}</strong></p>
   ```

   Do the same in the **Confirm signup** template, since a first sign-in sends
   that one instead.
5. Copy `.env.example` to `.env.local` and fill in the project URL and the
   publishable key (Project Settings → API), then restart `npx expo start`.
6. Optionally schedule `select public.purge_tombstones();` daily (pg_cron) to
   drop deletions older than 30 days.

The default Supabase email sender allows only a few messages an hour. Set up custom SMTP
before anyone besides you uses this.

## Signing in

Both apps sign in with an email code: `POST /auth/v1/otp`, then
`POST /auth/v1/verify` with `type: "email"`. The phone also accepts the magic
link. Sessions are refreshed with
`POST /auth/v1/token?grant_type=refresh_token`.

Every install has a **device id**: a UUID generated once and kept for good. It
is written to `updated_by` on every row the device changes. The device
upserts `public.devices` (`id`, `name`, `platform`, `last_seen_at`) when it
signs in and deletes that row when it signs out. `devices.tabs` is no longer
read or written; it stays only for app builds older than this document.

## The rows

### `public.spaces`

| Column | Notes |
| --- | --- |
| `id` | UUID. The desktop keeps its local Space id. |
| `name`, `icon`, `color` | `icon` is a Lucide name (`briefcase`), `color` is `#RRGGBB`. |
| `position` | Fractional index (below). Spaces are shown in `position` order. |
| `updated_at`, `updated_by`, `deleted_at` | See *Writing*. |

A Space's cookie jar and logins belong to each device and are not synced.

### `public.items`

| Column | Notes |
| --- | --- |
| `id` | UUID. For a today tab, the Avora tab GUID. |
| `space_id` | The owning Space. |
| `kind` | `today`, `pinned`, `favorite` or `folder`. |
| `parent_id` | A pinned item's folder; null at the top level. Folders hold only pinned items. |
| `url`, `title` | `http(s)` only; `url` is empty only for folders. For a today tab, where it was last left. For a pinned item or Favorite, the destination the user saved, **not** wherever its tab has wandered since. |
| `favicon_url` | The page's icon, fetched by each device itself. |
| `preview_path`, `preview_updated_at` | The tab's preview image, below. |
| `position` | Fractional index within `(space_id, kind, parent_id)`. |
| `last_active_at` | When any device last showed it. Informational; never used to resolve conflicts. |

The phone's "pinned tabs" are `pinned` items; its everyday tabs are `today`
items. "Clear Today" tombstones the current Space's `today` items. A device
must not upload `chrome://`, `file://`, `about:` or other local pages, or
incognito and other private tabs.

### Positions

`position` is a fractional index: a string that sorts between its neighbours
bytewise, so two devices can insert or move items without renumbering anyone
else's. Use the base-62 scheme of the
[`fractional-indexing`](https://github.com/rocicorp/fractional-indexing)
package (`generateKeyBetween(a, b)`); the desktop implements the same scheme.
Compare positions bytewise, never with a locale-aware comparison. If two rows
have the same position, order them by `id`.

## Reading

1. **Full load** on sign-in, or when the cursor is older than 30 days:
   `GET /rest/v1/spaces?select=*` and `GET /rest/v1/items?select=*`, tombstones
   included. Remember the largest `updated_at` seen as the cursor.
2. **Catch-up** on launch, on returning to the foreground and on reconnect:
   `GET /rest/v1/items?updated_at=gte.{cursor − 60 s}&order=updated_at` (same
   for `spaces`). The overlap is deliberate: a transaction that committed late
   can carry an `updated_at` a little older than one already seen. Applying a
   row twice is harmless.
3. **Live**: subscribe to Realtime `postgres_changes` on both tables (the phone
   does this). The desktop may poll the catch-up query instead, every few
   seconds while it is in front and less often otherwise.

To apply a row, replace the local copy wholesale with the server's, unless
the local copy has an unsent change, which will win when it is sent. A row
whose `updated_by` is this device and whose `updated_at` is no newer than what
this device last saw is its own write coming back; skip it.

## Writing

Send only the rows that changed, and only the columns that changed, through
`sync_items` (or `sync_spaces`):

```http
POST {SUPABASE_URL}/rest/v1/rpc/sync_items
apikey: {PUBLISHABLE_KEY}
Authorization: Bearer {access_token}
Content-Type: application/json

{"changes": [{"id": "…", "title": "New title", "updated_by": "{device id}"}]}
```

(`supabase.rpc('sync_items', { changes })` on the phone.) A plain PostgREST
upsert won't do: it has to satisfy every required column even when the row
already exists.

- A key that's present, even as `null`, is written; a missing key is left as
  it is. Objects in one call can carry different keys.
- A new row must include `space_id`, `kind`, `url` (unless it's a folder) and
  `position`.
- Every change carries `updated_by`.
- **Delete** by sending `"deleted_at"` with any timestamp; the server
  stamps its own. There is no row delete. A tombstoned row stays tombstoned
  unless a write explicitly sends `"deleted_at": null`, which only "reopen
  closed tab" does.
- The server sets `updated_at` (and `deleted_at`) from its own clock, so the
  last write to reach it wins, column by column.
- Debounce: a burst of changes goes out as one request a second or two later.
  Never re-upload a remote change that was just applied.
- Navigating inside a today tab updates its `url` and `title`. Navigating in a
  tab opened from a pinned item or Favorite does not change the item.
- Quitting, closing the last window or crashing must never be uploaded as
  closing tabs.

### First sign-in on a device that already has tabs

Merge, never replace. A local Space whose name matches an account Space
(ignoring case) becomes that Space; the other local Spaces are uploaded. Within
a Space, a local item matching an existing item of the same `kind` and `url`
adopts that item's id; the rest are uploaded. If the account has no Spaces, the
device uploads its own (the phone uploads one Space called "Personal").

## Previews

A preview lets a tab show something immediately while its page loads, on any
device.

- A JPEG of the visible page, at most 640 px wide, quality around 60, and at
  most 256 KB. It is stored at `tab-previews/{user_id}/{item_id}.jpg`
  (`POST /storage/v1/object/tab-previews/{path}` with `x-upsert: true`), after
  which the item's `preview_path` and `preview_updated_at` are set.
- Capture after the page finishes loading and when the user leaves the tab,
  at most once a minute per tab. Only the device showing the tab captures it.
- **Private pages get no preview.** Don't capture a page that has a password
  field, a page served with `Cache-Control: no-store` (banks, mail and
  account pages usually are), or an incognito tab. When a page stops
  qualifying, delete its object and set `preview_path` to null. Each app also
  has a switch to turn previews off entirely.
- Fetch with `GET /storage/v1/object/authenticated/tab-previews/{path}` and
  cache by `preview_updated_at`. Delete the object when its item is
  tombstoned.

## Keeping tabs ready (per device, not synced)

- A tab that arrives from another device opens unloaded. Show its preview
  until it is selected and loaded.
- Keep the most recently used tabs loaded within a small budget and unload
  the rest: about 12 per window on a Mac, about 3 on the phone. Never unload
  the active tab, a tab playing audio, or a tab with unsaved form input.
- When an unloaded tab is selected, show its preview at once and replace it
  when the page first paints.
- Don't reload a tab just because it was selected. If it is loaded and its URL
  already matches the shared row, reuse it. If another device navigated it
  while it was loaded here, navigate it the next time it's selected, never
  while it is on screen.
