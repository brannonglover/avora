#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
AVORA_ROOT="$(dirname "$SCRIPT_DIR")"
AVORA_SRC="$AVORA_ROOT/src"
CHROMIUM_SRC="$AVORA_ROOT/chromium/src"

if [ ! -d "$AVORA_SRC" ]; then
  echo "ERROR: $AVORA_SRC not found."
  exit 1
fi

if [ ! -d "$CHROMIUM_SRC" ]; then
  echo "ERROR: $CHROMIUM_SRC not found. Run gclient sync first."
  exit 1
fi

echo "==> Copying Avora source files into Chromium tree..."

# Only files whose contents actually differ are copied, and copies keep their
# source timestamp (cp -p).  Ninja decides what to rebuild from mtimes, so
# re-stamping an unchanged file marks every object that includes it dirty --
# one touch of a widely included header like chrome/browser/ui/tab_helpers.h
# turns a two-file change into a multi-hour rebuild of chrome/browser.
count=0
cd "$AVORA_SRC"
find . -type f | while read -r rel; do
  dest="$CHROMIUM_SRC/${rel#./}"
  if cmp -s "$rel" "$dest"; then
    continue
  fi
  mkdir -p "$(dirname "$dest")"
  cp -p "$rel" "$dest"
  count=$((count + 1))
  echo "    $rel"
done

echo "==> Done. Avora source files synced into Chromium tree."
