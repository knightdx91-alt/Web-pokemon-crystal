#!/usr/bin/env bash
# Generate the single-instruction executor and link the SingleStepTests harness wasm.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

PYTHONPATH="$ROOT/tools/recompiler" python3 tools/optest/optest_gen.py > tools/optest/optest_gen.c
clang --target=wasm32 -nostdlib -ffreestanding -O1 -I runtime/include \
  -Wl,--no-entry -Wl,--export-all -Wl,--allow-undefined \
  tools/optest/testbus.c runtime/hal/alu.c tools/optest/optest_gen.c \
  -o tools/optest/optest.wasm
echo "optest.wasm built"
