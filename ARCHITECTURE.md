# Architecture

This document is the source of truth for how the port works. Read it before touching
the recompiler or the HAL.

## Goal restated

Turn the `pret/pokecrystal` **SM83 assembly** into a **native WebAssembly module** in
which the game's code runs as compiled instructions (no opcode interpreter), backed by
a reimplemented Game Boy **hardware layer**. Three pieces, three boundaries:

```
  ┌─────────────────────────────────────────────────────────────────┐
  │  BUILD-TIME (Python recompiler)                                   │
  │                                                                   │
  │  pokecrystal.gbc ─┐                                               │
  │  pokecrystal.sym ─┼─► [recompiler] ─► generated/rom_code.c        │
  │  pokecrystal.map ─┘                  generated/rom_data.c         │
  │                                      generated/labels.h           │
  └─────────────────────────────────────────────────────────────────┘
                                  │  compiled by clang→wasm32 / emscripten
                                  ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │  RUNTIME (C → WASM)                                               │
  │                                                                   │
  │   generated code (game logic)  ─┐                                 │
  │   runtime/hal/bus.c   (MBC3, WRAM/VRAM/OAM/HRAM, banking) ◄──────┐│
  │   runtime/hal/ppu.c   (BG/window/sprite → RGBA framebuffer)      ││
  │   runtime/hal/apu.c   (4 channels → PCM ring buffer)             ││
  │   runtime/hal/timer.c (DIV/TIMA), interrupt.c (IF/IE, VBlank…)   ││
  │   runtime/hal/input.c (joypad register)                          ││
  └─────────────────────────────────────────────────────────────────┘
                                  │  exports: gb_init/gb_run_frame/gb_framebuffer…
                                  ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │  PRESENTATION (web/ — JS, throwaway/replaceable)                  │
  │   canvas blit ◄ framebuffer │ WebAudio ◄ PCM │ keyboard/gamepad → │
  │   rAF loop calls gb_run_frame() once per VBlank (~59.7 Hz)        │
  └─────────────────────────────────────────────────────────────────┘
```

## 1. Why static recompilation is *not* an emulator

A Game Boy emulator's hot loop is *fetch opcode → decode → dispatch → execute*,
millions of times per second. That decode/dispatch is the cost. **Static
recompilation removes it entirely**: at build time we decode every instruction once
and emit the equivalent C. At runtime, `ld a, [hl]` is already a C statement
(`cpu.a = bus_read(HL());`) compiled to WASM — there is no opcode byte to decode.

What we *do* still implement is the **hardware** the code talks to (video/audio/
timers/banking). That is modeling, not CPU emulation, and it's irreducible for any
port that reuses the original game logic.

## 2. Source of truth: build the ROM, read the symbols

We don't disassemble a binary blind — `rgbds` gives us, for free:

- `pokecrystal.gbc` — the bytes (code + data) laid out in banks.
- `pokecrystal.sym` — `BB:AAAA Label` lines: every label's **bank** and **address**.
- `pokecrystal.map` — section layout per bank.

The `.sym` is the recompiler's seed: it tells us where code labels, data tables, and
jump targets live, so we can separate code from data reliably instead of guessing.

## 3. The recompiler (`tools/recompiler/`)

Pipeline, each stage a module:

1. **`symfile.py`** — parse `.sym`/`.map` → `Symbol(bank, addr, name)` table and a
   reverse address→label map. *(Implemented.)*
2. **`decode.py`** — SM83 instruction decoder: bytes → `Insn(mnemonic, operands,
   length, addr)`. Full 256 + CB-prefix opcode tables. *(Table scaffolded.)*
3. **`disasm.py`** — recursive-descent walk seeded by: the entry point (`$0100`), the
   RST vectors (`$00,$08,…,$38`), the interrupt vectors (`$40 VBlank, $48 STAT,
   $50 timer, $58 serial, $60 joypad`), and every code label in the `.sym`. Resolves
   each bank's `$4000–$7FFF` window using the symbol bank info. Produces basic blocks.
4. **`translate.py`** — per-instruction SM83 → C emitter. CPU state is a struct;
   memory access goes through `bus_read`/`bus_write`; flags are explicit fields.
5. **`emit.py`** — writes `generated/rom_code.c`, `generated/rom_data.c`,
   `generated/labels.h`.

### 3.1 Control-flow model

Each translated **basic block** becomes a `case` in a per-bank dispatch `switch(pc)`;
fall-through and direct jumps `goto` the next case label, indirect/computed jumps
(`jp hl`, jump tables) set `pc` and re-enter the dispatch. This makes **computed jumps
and self-referential jump tables work without resolving every target statically** —
the only thing the switch dispatches is *control flow*, never opcode decode. Banked
calls (`farcall`, `rst`, `homecall`) route through a small trampoline keyed on
`(bank, addr)`.

### 3.2 CPU state

```c
typedef struct {
    uint8_t a, f, b, c, d, e, h, l;   // flags f: Z N H C in bits 7..4
    uint16_t sp, pc;
    uint8_t  rom_bank, ram_bank;      // MBC3 state mirror
    int      ime;                     // interrupt master enable
    uint64_t cycles;                  // T-cycles, drives PPU/APU/timer catch-up
} CpuState;
```

Helpers `AF()/BC()/DE()/HL()` and flag macros live in `runtime/include/gb.h`.

### 3.3 Hard problems (and the chosen strategy)

| Problem | Strategy |
|---|---|
| Computed jumps / jump tables | dispatch `switch(pc)` per bank (§3.1) |
| Bank switching (MBC3) | `rom_bank`/`ram_bank` in bus.c; calls carry their bank |
| Code/data ambiguity | seed disasm from `.sym` labels + known vectors; data via `db/dw` regions |
| Cycle timing (PPU/IRQ) | each block advances `cpu.cycles`; HAL "catches up" at block ends |
| Self-modifying code | pokecrystal RAM code is rare; flagged + handled case-by-case |
| `halt`/`stop`/IME timing | modeled in the dispatch loop, interrupts checked at block boundaries |

## 4. The HAL (`runtime/hal/`)

Reimplements only what the game's code reads/writes via memory-mapped I/O.

- **`bus.c`** — the memory map + MBC3 banking. Routes reads/writes among ROM banks,
  VRAM, WRAM (banked), OAM, HRAM, I/O registers ($FF00–$FF7F), and IE. The single
  choke point `bus_read`/`bus_write` that generated code calls.
- **`ppu.c`** — scanline renderer: background + window (tilemaps, scroll, CGB tile
  attributes/palettes) and sprites (OAM, 8×8/8×16, priority). Emits a 160×144 RGBA
  framebuffer. Drives the STAT/LY/VBlank timing the game polls.
- **`apu.c`** — 4 channels (2 pulse, wave, noise) → PCM ring buffer for WebAudio.
- **`timer.c`** — DIV / TIMA / TMA / TAC.
- **`interrupt.c`** — IF/IE, dispatch of the 5 interrupts, IME handshake.
- **`input.c`** — joypad register ($FF00) from the JS-set button bitmask.
- **`gb.c`** — exported entrypoints (`gb_init`, `gb_run_frame`, `gb_set_buttons`,
  `gb_framebuffer`, `gb_audio_*`) and the frame loop that runs translated code until
  VBlank, ticking the HAL.

CGB specifics (double-speed mode, VRAM bank, palette RAM, HDMA) are first-class —
Crystal is a Color game.

## 5. Presentation (`web/`)

Deliberately thin and replaceable: load the `.wasm`, read the exported framebuffer
into a canvas `ImageData`, feed exported PCM to a WebAudio `AudioWorklet`, map
keyboard/gamepad to `gb_set_buttons`, and call `gb_run_frame()` from `requestAnimationFrame`.
No game logic here — same discipline as the sibling Awakened-Calamity project.

## 6. Build pipeline (`build/`)

`fetch_sources.sh` (clone pokecrystal @ pin) → `build_rom.sh` (rgbds) →
`recompile.sh` (Python recompiler) → `build_wasm.sh` (clang/emscripten link of
generated C + HAL) → `serve.sh`. Orchestrated by the top-level `Makefile`.

## 7. Validation

Determinism + faithfulness checks (planned, `docs/ROADMAP.md`): compare HAL framebuffer
hashes at known frames against a reference emulator trace of the same input script.
