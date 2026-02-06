#!/usr/bin/env bash
set -euo pipefail

# Build the browser emulator (C -> WebAssembly) using Emscripten.
#
# Output:
#   emulator/web/pong.js
#   emulator/web/pong.wasm

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# Sanity check
command -v emcc >/dev/null 2>&1 || {
  echo "error: emcc not found. Install/activate Emscripten (emsdk) and ensure 'emcc' is on PATH." >&2
  exit 1
}

emcc \
  "$ROOT_DIR/src/game.c" \
  "$ROOT_DIR/emulator/src/panel_emu.c" \
  -O2 \
  -sASYNCIFY \
  -sALLOW_MEMORY_GROWTH \
  -o "$ROOT_DIR/emulator/web/pong.js"

echo "Built: emulator/web/pong.js and emulator/web/pong.wasm"
