#!/usr/bin/env bash
# Compile recompiled C + HAL -> web/pokecrystal.wasm using clang's wasm32 target.
# (emscripten is an alternative; this uses freestanding clang + wasm-ld.)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/web/pokecrystal.wasm"

command -v clang >/dev/null || { echo "clang not found"; exit 1; }

EXPORTS=(gb_init gb_run_frame gb_set_buttons gb_framebuffer
         gb_audio_samples gb_audio_available gb_audio_consume
         gb_dbg_pc gb_dbg_bank gb_dbg_halted gb_dbg_lcdc
         gb_dbg_traps gb_dbg_trap_pc gb_dbg_trap_bank
         gb_dbg_trace_head gb_dbg_trace_at
         gb_dbg_io gb_dbg_bgpal gb_dbg_objpal gb_dbg_vram
         gb_dbg_vramw gb_dbg_hdma gb_dbg_read
         gb_dbg_set_watch gb_dbg_watch_head gb_dbg_watch_at gb_dbg_trace_aux
         gb_dbg_serve_ly gb_dbg_serve_copies gb_dbg_vbl_ly gb_dbg_vec_ly gb_dbg_preamble_cyc)
EXPORT_FLAGS=""
for e in "${EXPORTS[@]}"; do EXPORT_FLAGS+=" -Wl,--export=$e"; done

clang --target=wasm32 -nostdlib -O2 -ffreestanding \
  -I"$ROOT/runtime/include" \
  -Wl,--no-entry -Wl,--allow-undefined $EXPORT_FLAGS \
  -Wl,--export=__heap_base \
  "$ROOT"/runtime/hal/*.c "$ROOT"/generated/*.c \
  -o "$OUT"
echo "wasm -> $OUT"
