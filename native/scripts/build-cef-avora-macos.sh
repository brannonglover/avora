#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$ROOT_DIR/native/cef-project/build-make"
JOBS="$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"

cmake --build "$BUILD_DIR" --target Avora -- -j"$JOBS"
