#!/bin/bash
# Packages the built Avora.app into the DMG the site's download buttons serve,
# and records its size and checksum in website/release.json.
#
#   npm run dmg            # from website/; uses chromium/src/out/Default
#   npm run dmg -- <out>   # another build directory, e.g. out/Signing
#
# Set CODESIGN_IDENTITY to a Developer ID to sign with it; otherwise the copy is
# signed ad hoc. Either way the signature is redone on a staged copy, because a
# build directory's app is usually out of step with its own signature.
set -euo pipefail

SITE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
REPO_DIR="$(cd "$SITE_DIR/.." && pwd)"
OUT_NAME="${1:-out/Default}"
APP="$REPO_DIR/chromium/src/$OUT_NAME/Avora.app"
IDENTITY="${CODESIGN_IDENTITY:--}"

[[ -d "$APP" ]] || { echo "No Avora.app at $APP. Run npm run build first." >&2; exit 1; }

VERSION="$(node -p "require('$REPO_DIR/package.json').version")"
CHROMIUM="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP/Contents/Info.plist")"
FILE="Avora-$VERSION-arm64.dmg"
DEST_DIR="$SITE_DIR/public/downloads"
DEST="$DEST_DIR/$FILE"

STAGE="$(mktemp -d "${TMPDIR:-/tmp}/avora-dmg.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT

echo "Staging $APP"
mkdir "$STAGE/root"
ditto "$APP" "$STAGE/root/Avora.app"
ln -s /Applications "$STAGE/root/Applications"

echo "Signing with identity: $IDENTITY"
codesign --force --deep --sign "$IDENTITY" \
  --preserve-metadata=entitlements,flags "$STAGE/root/Avora.app"
codesign --verify --deep --strict "$STAGE/root/Avora.app"

mkdir -p "$DEST_DIR"
rm -f "$DEST"
echo "Compressing"
hdiutil create -quiet -volname "Avora" -srcfolder "$STAGE/root" \
  -fs APFS -format ULFO -ov "$DEST"

SIZE="$(stat -f %z "$DEST")"
SHA256="$(shasum -a 256 "$DEST" | cut -d' ' -f1)"

# `notarized` is left as it was: it records a notarization that happens outside
# this script.
node -e '
  const fs = require("fs");
  const path = process.argv[1];
  const release = JSON.parse(fs.readFileSync(path, "utf8"));
  Object.assign(release, {
    version: process.argv[2],
    chromium: process.argv[3],
    file: process.argv[4],
    sizeBytes: Number(process.argv[5]),
    sha256: process.argv[6],
  });
  fs.writeFileSync(path, JSON.stringify(release, null, 2) + "\n");
' "$SITE_DIR/release.json" "$VERSION" "$CHROMIUM" "$FILE" "$SIZE" "$SHA256"

echo "Wrote $DEST ($((SIZE / 1000000)) MB)"
