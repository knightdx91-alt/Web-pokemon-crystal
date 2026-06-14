#!/usr/bin/env bash
# Clone pokecrystal at the pinned commit into vendor/ (git-ignored).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PIN="$(cat "$ROOT/POKECRYSTAL_PIN.txt")"
DEST="$ROOT/vendor/pokecrystal"

if [ -d "$DEST/.git" ]; then
  echo "vendor/pokecrystal exists; checking out pin $PIN"
  git -C "$DEST" fetch --depth 1 origin "$PIN"
else
  mkdir -p "$ROOT/vendor"
  git clone https://github.com/pret/pokecrystal "$DEST"
fi
git -C "$DEST" checkout "$PIN"
echo "pokecrystal @ $PIN ready."
