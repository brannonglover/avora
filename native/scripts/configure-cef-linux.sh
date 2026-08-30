#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CEF_PROJECT_DIR="$ROOT_DIR/native/cef-project"
BUILD_DIR="$CEF_PROJECT_DIR/build-make"

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "This script is for Linux."
  exit 1
fi

ARCH="$(uname -m)"

PYTHON_BIN="${PYTHON_EXECUTABLE:-$(command -v python3 || true)}"
if [[ -z "$PYTHON_BIN" ]]; then
  echo "Could not find a Python 3 interpreter."
  exit 1
fi

CC="${CC:-gcc}"
CXX="${CXX:-g++}"

PYTHON_EXECUTABLE="$PYTHON_BIN" cmake -S "$CEF_PROJECT_DIR" \
  -B "$BUILD_DIR" \
  -G "Unix Makefiles" \
  -DPROJECT_ARCH="$ARCH" \
  -DWITH_EXAMPLES=On \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$CC" \
  -DCMAKE_CXX_COMPILER="$CXX"
