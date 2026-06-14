# web-pokemon-crystal — pipeline orchestration. See ARCHITECTURE.md §6.
.PHONY: sources rom recompile wasm serve clean test optest

sources:   ; bash build/fetch_sources.sh
rom:       ; bash build/build_rom.sh
recompile: ; bash build/recompile.sh
wasm:      ; bash build/build_wasm.sh
serve:     ; cd web && python3 -m http.server 8000

# fast checks (no rgbds needed): opcode coverage + HAL compiles under wasm32
test:
	cd tools/recompiler && python3 decode.py >/dev/null && python3 coverage.py
	for f in runtime/hal/*.c; do \
	  clang --target=wasm32 -nostdlib -ffreestanding -Iruntime/include -fsyntax-only $$f || exit 1; \
	done
	@echo "OK: opcode coverage clean, HAL compiles"

# SingleStepTests instruction-correctness sweep (needs network on first run to fetch
# the test vectors; cached in /tmp/sm83 afterwards).
optest:
	bash tools/optest/build_optest.sh
	node tools/optest/optest.mjs

clean:
	rm -rf generated/*.c build/rom web/pokecrystal.wasm
