#!/usr/bin/env bash
# Generate generated/rom_embed.S: embeds the built ROM's bytes directly into the
# wasm (.incbin) so the module is self-contained — no separate ROM file to load.
# The embedded bytes are the game's own data (graphics/maps/text/music), the same
# content as the ROM, just packaged inside the .wasm.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ROM="$ROOT/build/rom/pokecrystal.gbc"
OUT="$ROOT/generated/rom_embed.S"

[ -f "$ROM" ] || { echo "ROM not found; run build/build_rom.sh first"; exit 1; }
SZ=$(stat -c%s "$ROM")
mkdir -p "$ROOT/generated"
cat > "$OUT" <<EOF
/* generated — embeds the ROM data into the wasm (do not edit) */
	.data
	.globl gb_embedded_rom
gb_embedded_rom:
	.incbin "$ROM"
	.size gb_embedded_rom, $SZ
	.globl gb_embedded_rom_len
gb_embedded_rom_len:
	.int $SZ
	.size gb_embedded_rom_len, 4
EOF
echo "embedded $SZ-byte ROM -> generated/rom_embed.S"
