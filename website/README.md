# Avora website

The marketing site: one Next.js page laid out like an Avora window, with the
sidebar, the rounded content card and a Spaces bar that re-tints the page.

## Run it

```sh
npm install
npm run dev     # http://localhost:3000
```

From the repo root, `npm run site` does the same as `npm run dev`.

## Releases and updates

`releases.json` is the record of every build, and everything public reads it: the
download buttons use the newest published row, and `/appcast.xml`, the feed every
installed copy of Avora checks, lists the published rows with an update signature.
It's read at build time, so a change goes out with the deploy that carries it.

Installed copies update through [Sparkle](https://sparkle-project.org). They check
the feed every six hours, and when there's something new an update button appears
in the Space bar. Pressing it, or choosing Check for Updates in the Avora menu,
opens a window with the notes and the choice to install. Nothing interrupts the
page, and nothing changes until the person agrees.

To ship a version, from the repo root:

```sh
npm version minor
npm run release
```

`build-scripts/release.sh` checks everything it can in seconds, then builds,
signs, notarizes and uploads the DMG to GitHub Releases, which is where it lives
because it's too big for Vercel's `public/`. It ends by opening the admin at
<http://localhost:3000/admin/releases> with the draft, whose notes start from the
commits since the last release. Edit them and press Publish. That refuses a build
that isn't notarized, isn't signed for updates, or doesn't match what's on GitHub.
Otherwise it commits and pushes `releases.json`, and that deploy sends the update
out.

It needs a clean `main`, because the build takes what's on disk. If something fails
after packaging, run it again: it reuses the notarized DMG of that commit.

To pull a bad release, press Discard on it, which pushes the same way. The feed and
the download buttons go back to the release before it, and copies that already installed it stay
on it until a higher version ships, because Sparkle never goes backwards.

The admin only exists under `npm run dev`. The deployed site answers 404, and it
has no way to write `releases.json` anyway.

`npm run dmg` on its own makes a test build. With no identity it's ad-hoc signed,
and without `sparkle-public-key` it has no updater, so its draft can't be published.

`npm run dev` caches `/appcast.xml` once it has served it, so restart it to see a
change to `releases.json` there.

## Before this goes public

- **Back up the update key.** `npm run release` creates it the first time and
  commits `sparkle-public-key`. Every update is signed with it, and installed
  copies only trust the public half they shipped with, so losing the private key
  means no copy in the wild can be updated again:

  ```sh
  .sparkle/bin/generate_keys -x <somewhere safe>
  ```

- **Sign and notarize.** With no identity the DMG is ad-hoc signed, so macOS
  stops the first launch. A release needs a Developer ID Application
  certificate, a Developer ID provisioning profile for `com.avora.browser`, and
  notary credentials saved once with `xcrun notarytool store-credentials`.
  `npm run release` expects them as `avora-notary` and
  `~/Downloads/Avora_Developer_ID.provisionprofile`, and `NOTARY_PROFILE` and
  `PROVISIONING_PROFILE` override those. The script signs the app inside out, then notarizes and staples the DMG. It sets
  `notarized` in `releases.json` from what actually happened, which removes the
  Open Anyway note from the page, and a build that isn't accepted publishes
  nothing. The app's `application-identifier` entitlement is why the profile is
  needed: without one that lists the Developer ID certificate, macOS kills the
  app at launch, even though it passes notarization. The development profile
  doesn't list it.

## Analytics

PostHog counts page views and clicks on the three Download buttons, as a
`download_clicked` event with `placement` (`hero`, `download-section` or
`sidebar`) and `version`. It only runs when `NEXT_PUBLIC_POSTHOG_KEY` is set: in
Vercel for the live site, and in `.env.local` if you want local visits counted.
Events go through `/ingest` on the site, which `next.config.ts` forwards to
PostHog, so ad blockers don't hide most visitors. The project is assumed to be
in PostHog's US region; `lib/posthog-hosts.ts` says what to change for EU.

The site shares Glover Labs' one PostHog project (350420) with Cavaro, since
the plan allows only one, so `NEXT_PUBLIC_POSTHOG_KEY` is that project's `phc_`
key. `npm run dashboard` creates the "Avora downloads" dashboard there, or
updates it after you change an insight in `scripts/posthog-dashboard.mjs`. Every
chart is limited to events from www.avorabrowser.com, so Cavaro's traffic and
local visits don't count. It reads the personal API key from
`~/.config/posthog/personal-key`, or from `.env.local`.

A click is intent, not a finished download, since the DMG comes from GitHub
Releases. GitHub's own count is on each release asset's `download_count`.

## Screenshots

Everything in `public/shots` is a real capture of `Avora.app`, taken from a
throwaway profile so no personal tabs or accounts appear:

- Launch `Avora` with `--user-data-dir=<temp dir>` and `--remote-debugging-port`,
  seed `avora.spaces` and `avora.sidebar_items` in `Default/Preferences`, and
  delete `Default/Sessions` so the window opens on the active Space.
- Capture one window with `screencapture -x -o -l <window id>`.
- `mini.png` is two captures composited with ffmpeg. Mini windows only open for
  links arriving from other apps, so send `kAEGetURL` to the instance's pid
  rather than using `open`, which can reach your real Avora.

Don't capture the import dialog: it lists the real browser profiles on the Mac,
even from a throwaway profile.

`next/image` caches by URL, so delete `.next/cache/images` and
`.next/dev/cache/images` after replacing a screenshot under the same name.
