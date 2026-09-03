#!/usr/bin/env bash
set -euo pipefail
export PATH="$PATH:/Users/bglover/depot_tools"
cd /Users/bglover/projects/avora/chromium/src
autoninja -C out/Default chrome
