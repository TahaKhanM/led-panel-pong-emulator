#!/usr/bin/env bash
set -euo pipefail

# Serve the emulator/web folder over HTTP.
# Browsers typically block loading .wasm from file:// URLs.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR/emulator/web"

if [[ ! -f pong.js || ! -f pong.wasm ]]; then
  echo "Build the emulator first: ./emulator/scripts/build_web.sh" >&2
  exit 1
fi
python3 -m http.server 8000 --bind 127.0.0.1
