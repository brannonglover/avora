#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
AVORA_SRC="$ROOT_DIR/native/avora"
CEF_EXAMPLES="$ROOT_DIR/native/cef-project/examples"
MARKER="# Avora example (managed by avora repo)"

if [[ ! -d "$AVORA_SRC" ]]; then
  echo "Avora source not found at $AVORA_SRC"
  exit 1
fi

if [[ ! -d "$ROOT_DIR/native/cef-project" ]]; then
  echo "CEF project not found. Clone it first:"
  echo "  git clone https://github.com/chromiumembedded/cef-project.git native/cef-project"
  exit 1
fi

rm -rf "$CEF_EXAMPLES/avora"
mkdir -p "$CEF_EXAMPLES/avora"
cp -a "$AVORA_SRC/." "$CEF_EXAMPLES/avora/"

CMAKE_FILE="$CEF_EXAMPLES/CMakeLists.txt"
if ! grep -q "$MARKER" "$CMAKE_FILE"; then
  cat >> "$CMAKE_FILE" <<EOF

$MARKER
add_subdirectory(avora)
EOF
fi

echo "Synced Avora example to $CEF_EXAMPLES/avora"
