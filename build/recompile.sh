#!/usr/bin/env bash
# Run the static recompiler: ROM + sym -> generated/ C.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ROM="$ROOT/build/rom/pokecrystal.gbc"
SYM="$ROOT/build/rom/pokecrystal.sym"

[ -f "$ROM" ] || { echo "run build/build_rom.sh first"; exit 1; }
# CODE_BANKS: which banks to translate as code (broadened as translate.py coverage grows)
CODE_BANKS="${CODE_BANKS:-0}"
python3 "$ROOT/tools/recompiler/main.py" --rom "$ROM" --sym "$SYM" \
        --out "$ROOT/generated" --code-banks "$CODE_BANKS"
