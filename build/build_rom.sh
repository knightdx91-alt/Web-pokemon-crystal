#!/usr/bin/env bash
# Assemble pokecrystal -> .gbc + .sym + .map via rgbds (must be installed).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/vendor/pokecrystal"

# rgbds v1.0.1 is required (pokecrystal's rgbdscheck.asm enforces >= 1.0.0). Build
# from source if absent: needs a C++20 compiler, make, bison, and libpng. e.g.
#   git clone --branch v1.0.1 https://github.com/gbdev/rgbds && cd rgbds \
#     && make -j && sudo make install PREFIX=/usr/local
command -v rgbasm >/dev/null || { echo "rgbds not found; build/install rgbds v1.0.1 first"; exit 1; }
[ -d "$SRC" ] || { echo "run build/fetch_sources.sh first"; exit 1; }

make -C "$SRC" pokecrystal.gbc
mkdir -p "$ROOT/build/rom"
cp "$SRC/pokecrystal.gbc" "$SRC/pokecrystal.sym" "$SRC/pokecrystal.map" "$ROOT/build/rom/"
echo "ROM + sym + map -> build/rom/"
