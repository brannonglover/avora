#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
AVORA_ROOT="$(dirname "$SCRIPT_DIR")"
CHROMIUM_SRC="$AVORA_ROOT/chromium/src"
BUILD_DIR="$CHROMIUM_SRC/out/Default"

export PATH="$PATH:/Users/bglover/depot_tools"

usage() {
  echo "Usage: $0 [icons|copy-sources|gen|build|full|cleanup]"
  echo ""
  echo "Commands:"
  echo "  icons           Regenerate branding images from branding/avora_icon_1024.png"
  echo "  copy-sources    Copy Avora source files into the Chromium tree"
  echo "  gen             Run gn gen with Avora args"
  echo "  build           Run autoninja to build chrome"
  echo "  full            copy-sources -> gen -> build"
  echo "  cleanup         Remove stale Chromium.app build artifacts"
  exit 1
}

icons() {
  "$SCRIPT_DIR/generate-icons.sh"
}

copy_sources() {
  "$SCRIPT_DIR/copy-sources.sh"
}

gen() {
  echo "==> Running gn gen..."
  mkdir -p "$BUILD_DIR"
  cp "$SCRIPT_DIR/args.gn" "$BUILD_DIR/args.gn"
  cd "$CHROMIUM_SRC"
  gn gen "$BUILD_DIR"
  echo "==> gn gen complete."
}

build() {
  echo "==> Building Avora (this will take a while)..."
  cd "$CHROMIUM_SRC"
  caffeinate autoninja -C "$BUILD_DIR" chrome
  refresh_icon_cache
  echo "==> Build complete!"
  echo "    Binary: $BUILD_DIR/Avora.app"
}

# The build replaces files inside Avora.app/Contents/Resources without changing
# the bundle's own modification date, so Launch Services and the Dock keep
# serving a cached app icon. Bumping the date and re-registering forces a reread.
refresh_icon_cache() {
  local app="$BUILD_DIR/Avora.app"
  local lsregister="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
  [ -d "$app" ] || return 0
  echo "==> Refreshing app icon cache..."
  touch "$app/Contents/Info.plist" "$app/Contents" "$app"
  [ -x "$lsregister" ] && "$lsregister" -f "$app"
  killall Dock 2>/dev/null || true
}

cleanup() {
  echo "==> Removing stale Chromium.app artifacts..."
  rm -rf "$BUILD_DIR/Chromium.app" "$BUILD_DIR"/Chromium\ Helper*.app
  echo "==> Cleanup complete."
}

case "${1:-}" in
  icons) icons ;;
  copy-sources) copy_sources ;;
  gen) gen ;;
  build) build ;;
  cleanup) cleanup ;;
  full) copy_sources; gen; build ;;
  *) usage ;;
esac
