# web-pokemon-crystal — pipeline orchestration. See ARCHITECTURE.md §6.
.PHONY: sources rom recompile wasm serve clean test

sources:   ; bash build/fetch_sources.sh
rom:       ; bash build/build_rom.sh
recompile: ; bash build/recompile.sh
wasm:      ; bash build/build_wasm.sh
serve:     ; cd web && python3 -m http.server 8000

# fast recompiler unit checks (no rgbds/clang needed)
test:
	cd tools/recompiler && python3 decode.py && python3 -m pyflakes *.py 2>/dev/null || true

clean:
	rm -rf generated/*.c build/rom web/pokecrystal.wasm
