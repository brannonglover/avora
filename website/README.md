# Avora website

The marketing site: one Next.js page laid out like an Avora window, with the
sidebar, the rounded content card and a Spaces bar that re-tints the page.

## Run it

```sh
npm install
npm run dev     # http://localhost:3000
```

From the repo root, `npm run site` does the same as `npm run dev`.

## The download

In development the buttons serve `public/downloads/Avora-<version>-arm64.dmg`,
which is too big for git and is ignored. Build it after building the browser:

```sh
npm run dmg     # from out/Default
```

The script re-signs a staged copy, because a build directory's `Avora.app`
usually fails `codesign --verify`, and records the size and checksum in
`release.json`. The site reads that file, so the numbers it shows stay true.
Rebuild the site after packaging.

## Before this goes public

- **Sign and notarize.** With no identity the DMG is ad-hoc signed, so macOS
  stops the first launch. A release needs a Developer ID Application
  certificate, a Developer ID provisioning profile for `com.avora.browser`, and
  notary credentials saved once with `xcrun notarytool store-credentials`:

  ```sh
  CODESIGN_IDENTITY="Developer ID Application: …" \
  PROVISIONING_PROFILE=<path to the profile> \
  NOTARY_PROFILE=<saved credentials name> \
  npm run dmg
  ```

  The script signs the app inside out, then notarizes and staples the DMG. It
  sets `notarized` in `release.json` from what actually happened, which removes
  the Open Anyway note from the page, and a build that isn't accepted publishes
  nothing. The app's `application-identifier` entitlement is why the profile is
  needed: without one that lists the Developer ID certificate, macOS kills the
  app at launch, even though it passes notarization. The development profile
  doesn't list it.
- **Host the DMG.** It's too big for Vercel's `public/`, so it goes on GitHub
  Releases, tagged with its version:

  ```sh
  gh release create v0.1.0 public/downloads/Avora-0.1.0-arm64.dmg \
    --repo brannonglover/avora --title "Avora 0.1.0"
  ```

  Then point `NEXT_PUBLIC_DOWNLOAD_URL` in `.env.production` at the new asset.
  It's read at build time, so rebuild the site afterwards.

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
