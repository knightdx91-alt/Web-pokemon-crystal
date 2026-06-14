# web-pokemon-crystal

A browser-playable port of **Pokémon Crystal**, built by **statically recompiling**
the [`pret/pokecrystal`](https://github.com/pret/pokecrystal) disassembly to
**WebAssembly** — *not* by shipping a Game Boy emulator.

> **Approach:** the original Game Boy (SM83) assembly is translated **ahead of time**
> into C, which compiles to a native `.wasm` module. There is **no runtime opcode
> interpreter** — every Game Boy instruction becomes real, compiled WASM code. The
> Game Boy *hardware* (PPU, APU, timers, MBC banking) is reimplemented as a thin
> Hardware Abstraction Layer (HAL) in C. See [`ARCHITECTURE.md`](ARCHITECTURE.md).

## Why static recompilation (not an emulator)

| | Emulator-in-WASM | **Static recompilation (this repo)** | Full rewrite |
|---|---|---|---|
| Runtime CPU interpretation | yes (slow, the hot loop) | **no — code is pre-translated** | n/a |
| Effort | low | high | very high |
| Faithfulness | exact | exact (same code path) | re-derived |
| "No emulator" goal | ❌ | ✅ (no opcode interpreter) | ✅ |

The HAL still *models* the Game Boy's video/audio/timer hardware — that part is
unavoidable for any port short of a full reimplementation — but the **game logic
runs as compiled native code**, which is the expensive part an emulator interprets.

## Status

🏗️ **Architecture / scaffolding phase.** Nothing is playable yet. The repository
structure, build pipeline, recompiler skeleton, and HAL interfaces are in place; the
instruction-translation table and HAL bodies are being filled in incrementally. See
[`docs/ROADMAP.md`](docs/ROADMAP.md).

## Layout

```
POKECRYSTAL_PIN.txt   — pinned upstream commit the recompiler is validated against
build/                — pipeline scripts (fetch sources, build ROM, recompile, build wasm)
tools/recompiler/     — SM83 assembly/ROM → C static recompiler (Python)
runtime/include/       — C headers: CPU state, bus, HAL interfaces (the contract)
runtime/hal/          — C: bus/MBC, PPU, APU, timers, interrupts, input, entrypoint
generated/            — recompiler output (C); git-ignored, produced by the build
web/                  — browser frontend: canvas + WebAudio + input, loads the .wasm
vendor/pokecrystal/   — upstream disassembly; git-ignored, fetched at pinned commit
```

## Building

Requires `rgbds` (to assemble the source ROM + symbol map), `clang` with a wasm32
target (or `emscripten`), `python3`, and `make`. See [`ARCHITECTURE.md`](ARCHITECTURE.md)
for the full pipeline and [`docs/ROADMAP.md`](docs/ROADMAP.md) for current limitations.

```sh
make sources   # fetch pokecrystal at the pinned commit
make rom       # assemble pokecrystal.gbc + .sym + .map via rgbds
make recompile # translate ROM+sym -> generated/ C
make wasm      # compile recompiled C + HAL -> web/pokecrystal.wasm
make serve     # serve web/ over HTTP
```

## ⚠️ Legal

This repo contains **no copyrighted ROM data**. `pokecrystal` is a from-scratch
disassembly; building it locally produces a ROM and assets that remain **Nintendo /
Game Freak / The Pokémon Company** intellectual property. This project is for
**personal / educational** use. Do not distribute built ROMs or extracted assets.
