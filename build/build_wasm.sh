#!/usr/bin/env bash
# Compile recompiled C + HAL -> web/pokecrystal.wasm using clang's wasm32 target.
# (emscripten is an alternative; this uses freestanding clang + wasm-ld.)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/web/pokecrystal.wasm"

command -v clang >/dev/null || { echo "clang not found"; exit 1; }

EXPORTS=(gb_init gb_run_frame gb_set_buttons gb_framebuffer
         gb_audio_samples gb_audio_available gb_audio_consume)
EXPORT_FLAGS=""
for e in "${EXPORTS[@]}"; do EXPORT_FLAGS+=" -Wl,--export=$e"; done

clang --target=wasm32 -nostdlib -O2 -ffreestanding \
  -I"$ROOT/runtime/include" \
  -Wl,--no-entry -Wl,--allow-undefined $EXPORT_FLAGS \
  -Wl,--export=__heap_base \
  "$ROOT"/runtime/hal/*.c "$ROOT"/generated/*.c \
  -o "$OUT"
echo "wasm -> $OUT"
