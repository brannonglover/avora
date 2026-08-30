#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CEF_PROJECT_DIR="$ROOT_DIR/native/cef-project"
BUILD_DIR="$CEF_PROJECT_DIR/build-make"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This script is for macOS."
  exit 1
fi

if [[ ! -d "$CEF_PROJECT_DIR/.git" ]]; then
  echo "Cloning cef-project..."
  git clone --depth 1 https://github.com/chromiumembedded/cef-project.git "$CEF_PROJECT_DIR"
fi

bash "$ROOT_DIR/native/scripts/sync-avora-example.sh"

ARCH="$(uname -m)"
if [[ "$ARCH" != "arm64" && "$ARCH" != "x86_64" ]]; then
  echo "Unsupported macOS architecture: $ARCH"
  exit 1
fi

PYTHON_BIN="${PYTHON_EXECUTABLE:-}"
if [[ -z "$PYTHON_BIN" ]]; then
  for candidate in /opt/homebrew/bin/python3.11 /opt/homebrew/bin/python3.10 /opt/homebrew/bin/python3.9 /usr/bin/python3; do
    if [[ -x "$candidate" ]]; then
      PYTHON_BIN="$candidate"
      break
    fi
  done
fi

if [[ -z "$PYTHON_BIN" ]]; then
  echo "Could not find a compatible Python 3.9-3.11 interpreter."
  exit 1
fi

PYTHON_EXECUTABLE="$PYTHON_BIN" cmake -S "$CEF_PROJECT_DIR" \
  -B "$BUILD_DIR" \
  -G "Unix Makefiles" \
  -DPROJECT_ARCH="$ARCH" \
  -DWITH_EXAMPLES=On \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/usr/bin/clang \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++
