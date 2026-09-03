#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
AVORA_ROOT="$(dirname "$SCRIPT_DIR")"
CHROMIUM_SRC="$AVORA_ROOT/chromium/src"
PATCHES_DIR="$AVORA_ROOT/chromium-patches"
BUILD_DIR="$CHROMIUM_SRC/out/Default"

export PATH="$PATH:/Users/bglover/depot_tools"

usage() {
  echo "Usage: $0 [apply-patches|gen|build|full|clean-patches]"
  echo ""
  echo "Commands:"
  echo "  apply-patches   Apply all patches from chromium-patches/"
  echo "  clean-patches   Reverse all applied patches"
  echo "  gen             Run gn gen with Avora args"
  echo "  build           Run autoninja to build chrome"
  echo "  full            apply-patches -> gen -> build"
  echo "  cleanup         Remove stale Chromium.app build artifacts"
  exit 1
}

apply_patches() {
  echo "==> Applying patches..."
  cd "$CHROMIUM_SRC"
  for patch in "$PATCHES_DIR"/*.patch; do
    [ -f "$patch" ] || continue
    echo "    Applying $(basename "$patch")"
    git apply --check "$patch" 2>/dev/null && git apply "$patch" || {
      echo "    WARNING: Patch $(basename "$patch") did not apply cleanly, trying with 3-way merge..."
      git apply --3way "$patch" || {
        echo "    ERROR: Failed to apply $(basename "$patch")"
        exit 1
      }
    }
  done
  echo "==> All patches applied."
}

clean_patches() {
  echo "==> Reversing patches..."
  cd "$CHROMIUM_SRC"
  for patch in "$PATCHES_DIR"/*.patch; do
    [ -f "$patch" ] || continue
    echo "    Reversing $(basename "$patch")"
    git apply --reverse "$patch" 2>/dev/null || echo "    (already clean or not applied)"
  done
  echo "==> Patches cleaned."
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
  echo "==> Build complete!"
  echo "    Binary: $BUILD_DIR/Avora.app"
}

cleanup() {
  echo "==> Removing stale Chromium.app artifacts..."
  rm -rf "$BUILD_DIR/Chromium.app" "$BUILD_DIR"/Chromium\ Helper*.app
  echo "==> Cleanup complete."
}

case "${1:-}" in
  apply-patches) apply_patches ;;
  clean-patches) clean_patches ;;
  gen) gen ;;
  build) build ;;
  cleanup) cleanup ;;
  full) apply_patches; gen; build ;;
  *) usage ;;
esac
