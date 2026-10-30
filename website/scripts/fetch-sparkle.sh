#!/bin/bash
# Fetches the Sparkle release Avora ships, into website/.sparkle. The framework
# goes into the app and its bin/ tools sign the updates, so the version and its
# checksum are pinned here.
set -euo pipefail

VERSION=2.10.1
SHA256=1a856d2a9edb8e628de88121d7be04cf0e2167f8641650b78fca00b14244cf27

SITE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$SITE_DIR/.sparkle"

[[ "$(cat "$DEST/.sha256" 2>/dev/null)" != "$SHA256" ]] || exit 0

STAGE="$(mktemp -d "${TMPDIR:-/tmp}/avora-sparkle.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT

echo "Fetching Sparkle $VERSION"
curl -fsSL -o "$STAGE/sparkle.tar.xz" \
  "https://github.com/sparkle-project/Sparkle/releases/download/$VERSION/Sparkle-$VERSION.tar.xz"

if [[ "$(shasum -a 256 "$STAGE/sparkle.tar.xz" | cut -d' ' -f1)" != "$SHA256" ]]; then
  echo "Sparkle $VERSION doesn't match its pinned checksum." >&2
  exit 1
fi

mkdir "$STAGE/sparkle"
tar -xJf "$STAGE/sparkle.tar.xz" -C "$STAGE/sparkle"
rm -rf "$DEST"
mv "$STAGE/sparkle" "$DEST"
echo "$SHA256" > "$DEST/.sha256"
