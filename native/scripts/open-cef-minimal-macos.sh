#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP_PATH="$ROOT_DIR/native/cef-project/build-make/Release/minimal.app"

if [[ ! -d "$APP_PATH" ]]; then
  echo "CEF minimal app not found at:"
  echo "$APP_PATH"
  echo
  echo "Run:"
  echo "  npm run configure"
  echo "  npm run build:minimal"
  exit 1
fi

open "$APP_PATH"
