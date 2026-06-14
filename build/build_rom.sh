#!/usr/bin/env bash
# Assemble pokecrystal -> .gbc + .sym + .map via rgbds (must be installed).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/vendor/pokecrystal"

command -v rgbasm >/dev/null || { echo "rgbds not found; install rgbds first"; exit 1; }
[ -d "$SRC" ] || { echo "run build/fetch_sources.sh first"; exit 1; }

make -C "$SRC" pokecrystal.gbc
mkdir -p "$ROOT/build/rom"
cp "$SRC/pokecrystal.gbc" "$SRC/pokecrystal.sym" "$SRC/pokecrystal.map" "$ROOT/build/rom/"
echo "ROM + sym + map -> build/rom/"
