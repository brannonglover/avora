# Tab sync

Avora accounts and synced tabs live in Supabase. Every browser install owns one
row in `public.devices` and overwrites that row's `tabs` snapshot whenever its
tabs change. Other devices on the same account read it. For now the phone only
reads: it shows each device's tabs in the tab switcher and doesn't upload its
own.

```
Avora on the Mac ──upsert its row──▶ public.devices ──select + Realtime──▶ Avora on the phone
```

## Setting up the Supabase project

1. Create a project, then apply `supabase/migrations/20261026000000_devices.sql`
   (`supabase db push`, or paste it into the SQL editor).
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

The default Supabase email sender allows only a few messages an hour. Set up custom SMTP
before anyone besides you uses this.

## The `devices` row

| Column | Written by | Notes |
| --- | --- | --- |
| `id` | the device | A UUID the device generates once and keeps for good. |
| `user_id` | the server | Defaults to `auth.uid()`; row-level security keeps every user to their own rows. |
| `name` | the device | Shown as the section title, e.g. "Brannon's MacBook Pro". |
| `platform` | the device | `macos`, `windows`, `linux`, `ios` or `android`. |
| `tabs` | the device | JSON array of tabs, below. Always the full set, never a diff. |
| `updated_at` | the server | Set by a trigger on every write, so "updated 2 min ago" is reliable. |

Each element of `tabs`:

```json
{
  "url": "https://example.com/page",
  "title": "Example page",
  "pinned": false,
  "space": "Work",
  "lastActiveAt": 1793000000000
}
```

`url` and `title` are required, and `pinned` defaults to false. `space` (the
desktop Space the tab lives in) and `lastActiveAt` (milliseconds since the epoch) are
optional. The phone sorts pinned tabs first, then by `lastActiveAt`. Readers
discard anything that isn't an `http(s)` URL, but writers should leave out
`chrome://`, `file://` and other local pages themselves.

## Writing from the desktop

The desktop signs in the same way, through a magic link or code from
`POST /auth/v1/otp` and `POST /auth/v1/verify`. It keeps the session
(access token and refresh token) in the Keychain, then upserts its row through
PostgREST:

```http
POST {SUPABASE_URL}/rest/v1/devices?on_conflict=id
apikey: {PUBLISHABLE_KEY}
Authorization: Bearer {access_token}
Content-Type: application/json
Prefer: resolution=merge-duplicates,return=minimal

{"id": "…", "name": "…", "platform": "macos", "tabs": [ … ]}
```

Debounce writes, since one upload every few seconds while tabs are changing is
plenty. Refresh the access token with `POST /auth/v1/token?grant_type=refresh_token` when it
expires. On sign-out, delete the row so the phone stops showing a stale
device.
