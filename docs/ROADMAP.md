# Roadmap

Static recompilation of pokecrystal → WASM, built **vertically**: get one bank
executing end-to-end through real hardware before widening coverage.

## Phase 0 — Architecture & scaffold ✅ (current)
- Repo structure, build pipeline, design docs.
- Recompiler skeleton: `.sym` parser ✅, SM83 decoder ✅, recursive disasm ✅,
  translator (representative subset) ✅, C emitter ✅, orchestrator ✅.
- HAL interfaces + skeletons: bus/MBC3, PPU, APU, timer/IRQ/input, ALU, entrypoints.
- Web frontend skeleton.

## Phase 1 — Make the pipeline run for real
1. Install `rgbds` in the build env; `make sources rom` to produce gbc + sym + map.
2. ✅ **Translator coverage complete**: all 500 defined opcodes translate with zero
   `trap()` (`make test` / `tools/recompiler/coverage.py`). ALU/CB/rotate/DAA helpers
   implemented in `runtime/hal/alu.c`; all HAL compiles under `--target=wasm32`.
3. `make recompile CODE_BANKS=0`; diff `generated/bank_00.c` against a known
   disassembly of bank 0 (home section) to validate real output (needs the ROM).
4. ✅ **Frame loop wired** (`gb.c`): per-instruction T-cycle accounting (decode
   `CYCLES` table + taken-branch penalties), `hal_catch_up()` called at every
   dispatch so PPU/APU/timers advance during long routines, HALT handling, and
   top-level interrupt servicing. `gb_run_frame()` runs until the PPU latches a
   VBlank, with a cycle budget guard. *Caveat:* `rom_exec()` dispatches bank 0
   only — the banked-call trampoline (`rom_call` → per-bank dispatch table) is
   Phase 3.

## Phase 2 — Hardware fidelity
- PPU scanline renderer: BG + window + sprites, CGB VRAM-bank attributes, palette RAM,
  8×16 sprites, priority. STAT/LY/LYC timing the game polls.
- MBC3 full: RAM enable, RTC latch + registers (Crystal uses the clock).
- OAM DMA, HDMA (CGB), double-speed mode.
- APU: 4 channels + frame sequencer → PCM.
- Correct joypad bit mapping.

## Phase 3 — Boot to title
- Translate all code banks (`CODE_BANKS=0-…`). Resolve banked-call trampoline
  (`rom_call` → generated per-bank dispatch table).
- Get past the copyright/intro to the title screen, then to overworld.

## Phase 4 — Validation & feel
- Determinism harness: hash framebuffer at known frames against a reference emulator
  trace driven by the same input script (ARCHITECTURE.md §7).
- Save (SRAM) persistence via browser storage. Audio latency tuning.
- Performance pass; gamepad; mobile touch controls.

## Known risks / open questions
- **Code/data separation** in untranslated banks — recursive disasm from `.sym` should
  cover it, but jump-table-only targets may need manual seeds.
- **Banked-call performance**: trampoline per farcall vs. a flattened dispatch.
- **`stop`/double-speed** timing edge cases.
- **Asset/legal**: builds stay local; nothing copyrighted is committed.
