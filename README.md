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

✅ **Boots and renders in color.** The real Pokémon Crystal ROM, statically
recompiled to WebAssembly, boots and renders the intro **in color**, runs the sound
engine, and animates sprites — stably for 16,000+ frames with **zero traps**. Every
defined SM83 opcode is validated against the SingleStepTests vectors (`make optest`).

What works: full instruction set, multi-bank dispatch, MBC3 banking, the PPU
(BG/window/sprites, CGB palettes, scanline timing), interrupts/timers, OAM-DMA and
CGB HDMA, a frame loop with cycle-accurate VBlank handling, keyboard + touch input,
and an optional self-contained build (ROM data embedded in the `.wasm`).

What's not done yet: **audio output** (the APU is a stub — silent), the **wasm is
large** (~31 MB; all 128 banks are translated, including data banks that become dead
code — trimming this is the main optimization left), and end-to-end title→gameplay
hasn't been click-verified in a real browser. See [`docs/ROADMAP.md`](docs/ROADMAP.md).

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
make web       # embed the ROM into a self-contained web/pokecrystal.wasm (~4 min)
make serve     # serve web/ over HTTP -> open http://localhost:8000
```

- `make web` builds the **self-contained** module (ROM data embedded; boots on its
  own). `make wasm` builds the **code-only** module (the page then loads a ROM file).
- **On a phone:** `make serve`, then open `http://<your-computer-LAN-IP>:8000` over
  the same Wi-Fi. The frontend fits the screen to the orientation and supports
  drag-to-move + on-screen buttons.

## ⚠️ Legal

`pokecrystal` is a from-scratch disassembly; building it locally produces a ROM and
assets that remain **Nintendo / Game Freak / The Pokémon Company** intellectual
property. This project is for **personal / educational use only**.

**Do not host it publicly.** The self-contained `.wasm` embeds the game's copyrighted
data; putting it on a public URL (GitHub Pages, etc.) is distribution of copyrighted
material. Keep it local (`make serve`) or private. Built ROMs, the embedded-ROM wasm,
and extracted assets are git-ignored and must not be committed or distributed.
