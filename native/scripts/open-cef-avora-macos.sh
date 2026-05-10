#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP_PATH="$ROOT_DIR/native/cef-project/build-make/Release/Avora.app"

if [[ ! -d "$APP_PATH" ]]; then
  echo "CEF Avora app not found at:"
  echo "$APP_PATH"
  echo
  echo "Run:"
  echo "  npm run cef:configure"
  echo "  npm run cef:build:avora"
  exit 1
fi

open "$APP_PATH"
