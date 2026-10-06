#!/usr/bin/env bash
set -euo pipefail
export PATH="$PATH:/Users/bglover/depot_tools"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# The Avora tree is an overlay: edits live in src/ and only reach the build
# once they are copied into chromium/src.  Building without this step compiles
# whatever was last synced by hand, so a run can report success while none of
# the current changes are in the binary.  copy-sources.sh copies only files
# whose contents differ, so the no-change case costs nothing.
"$SCRIPT_DIR/copy-sources.sh"

cd "$(dirname "$SCRIPT_DIR")/chromium/src"
autoninja -C out/Default chrome
