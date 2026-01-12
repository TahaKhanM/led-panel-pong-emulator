#!/usr/bin/env bash
set -euo pipefail

# Serve the emulator/web folder over HTTP.
# Browsers typically block loading .wasm from file:// URLs.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR/emulator/web"

python3 -m http.server 8000
