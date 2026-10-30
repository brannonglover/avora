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

A draft goes nowhere. To ship one:

1. Bump `version` in the root `package.json`.
2. Build the browser, then package it. This makes the DMG and adds a draft row,
   with notes started from the commits since the last release:

   ```sh
   CODESIGN_IDENTITY="Developer ID Application: …" \
   PROVISIONING_PROFILE=<path to the profile> \
   NOTARY_PROFILE=<saved credentials name> \
   npm run dmg
   ```

3. Upload the DMG. It's too big for Vercel's `public/`, so it goes on GitHub
   Releases, tagged with its version:

   ```sh
   gh release create v0.3.0 public/downloads/Avora-0.3.0-arm64.dmg \
     --repo brannonglover/avora --title "Avora 0.3.0"
   ```

4. Run `npm run dev`, open <http://localhost:3000/admin/releases>, write the notes
   and press Publish. It refuses a build that isn't notarized, isn't signed for
   updates, or doesn't match what's on GitHub.
5. Commit `releases.json` and deploy the site.

To pull a bad release, press Discard on it and deploy. The feed and the download
buttons go back to the release before it, and copies that already installed it stay
on it until a higher version ships, because Sparkle never goes backwards.

The admin only exists under `npm run dev`. The deployed site answers 404, and it
has no way to write `releases.json` anyway.

Without `sparkle-public-key`, `npm run dmg` still works for a test build: it makes
a DMG with no updater, and its draft can't be published.

`npm run dev` caches `/appcast.xml` once it has served it, so restart it to see a
change to `releases.json` there.

## Before this goes public

- **Make the update key, once.** Every update is signed with it, and installed
  copies only trust the public half they shipped with, so losing the private key
  means no copy in the wild can be updated again:

  ```sh
  bash scripts/fetch-sparkle.sh
  .sparkle/bin/generate_keys                 # saves the private key in your Keychain
  .sparkle/bin/generate_keys -p > sparkle-public-key
  .sparkle/bin/generate_keys -x <somewhere safe>   # back it up
  ```

  Commit `sparkle-public-key`. A Developer ID build refuses to package without it.
- **Sign and notarize.** With no identity the DMG is ad-hoc signed, so macOS
  stops the first launch. A release needs a Developer ID Application
  certificate, a Developer ID provisioning profile for `com.avora.browser`, and
  notary credentials saved once with `xcrun notarytool store-credentials`. The
  script signs the app inside out, then notarizes and staples the DMG. It sets
  `notarized` in `releases.json` from what actually happened, which removes the
  Open Anyway note from the page, and a build that isn't accepted publishes
  nothing. The app's `application-identifier` entitlement is why the profile is
  needed: without one that lists the Developer ID certificate, macOS kills the
  app at launch, even though it passes notarization. The development profile
  doesn't list it.

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
