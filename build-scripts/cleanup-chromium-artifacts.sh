#!/usr/bin/env bash
set -euo pipefail
OUT="/Users/bglover/projects/avora/chromium/src/out/Default"
rm -rf "$OUT/Chromium.app" "$OUT"/Chromium\ Helper*.app
echo "Removed old Chromium.app artifacts"
