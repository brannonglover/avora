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

The buttons serve `public/downloads/Avora-<version>-arm64.dmg`, which is too big
for git and is ignored. Build it after building the browser:

```sh
npm run dmg     # from out/Default
```

The script re-signs a staged copy, because a build directory's `Avora.app`
usually fails `codesign --verify`, and records the size and checksum in
`release.json`. The site reads that file, so the numbers it shows stay true.
Rebuild the site after packaging.

## Before this goes public

- **Sign and notarize.** The DMG is ad-hoc signed, so macOS stops the first
  launch. Run `CODESIGN_IDENTITY="Developer ID Application: …" npm run dmg`,
  notarize and staple it, then set `notarized` to `true` in `release.json`. That
  removes the Open Anyway note from the page.
- **Host the DMG.** It's too big for Vercel's `public/`. Upload it somewhere
  like GitHub Releases or Vercel Blob and set `NEXT_PUBLIC_DOWNLOAD_URL`.
- **Set the domain.** Set `NEXT_PUBLIC_SITE_URL` so social previews resolve.

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
