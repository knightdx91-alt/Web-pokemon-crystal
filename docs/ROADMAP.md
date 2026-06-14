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
1. ✅ **rgbds v1.0.1 builds from source** (build/ scripts) and `make sources rom`
   produces a **byte-perfect ROM** — SHA1 `f4cd194b…` matches upstream `roms.sha1`.
   `make recompile CODE_BANKS=0` translates real **bank 0: 1200 blocks, zero genuine
   opcode gaps** (the only traps are undefined opcodes reached by over-walking into
   data — a code/data-separation refinement, item 3). The recompiled bank compiles
   under wasm32 and the full runtime **runs 60 frames without hanging** in Node.
   Data banks are NOT emitted as C — ROM is served from `g_rom` at runtime.
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
- ✅ **PPU scanline renderer** (`ppu.c`): BG + window + sprites → RGBA framebuffer,
  CGB VRAM-bank tile attributes, palette RAM ($FF68-$FF6B via bus.c), 8×16 sprites,
  10-per-line limit, BG/OBJ priority + flips, DMG fallback shades, LCDC/STAT/LY/LYC
  timing with mode progression. *Refinements still due:* mid-scanline accuracy,
  OAM DMA source, exact STAT-interrupt edge timing.
- MBC3 full: RAM enable, RTC latch + registers (Crystal uses the clock).
- OAM DMA, HDMA (CGB), double-speed mode.
- APU: 4 channels + frame sequencer → PCM.
- Correct joypad bit mapping.

## Phase 3 — Boot to title
- ✅ **Multi-bank dispatch built**: per-block-return model + `rom_dispatch` trampoline
  (`generated/dispatch.c`) routing `cpu.pc` to `bank_NN` via a table indexed by
  `cpu.rom_bank` (home → bank 0). Cross-bank calls/returns work. Disassembler now
  splits blocks at `call`/`rst` so return addresses are block starts; symbols are
  seeded without the old (buggy) data-suffix filter.
- ✅ **Boot runs on the real ROM**: clears the LY-wait, WRAM/VRAM clears, CGB palette
  init, turns the LCD on (LCDC=0xe3), and executes thousands of blocks across banks
  0,1,2,5,0x58,0x66… `gb_init` sets post-boot CGB I/O state (LCD on). OAM DMA handled:
  `bus.c` $FF46 write copies to OAM; `hram_exec` emulates the HRAM OAM-DMA routine
  (RAM code can't be statically recompiled).
- ✅ **Boot crash root-caused and fixed**: it was NOT a translation divergence — the
  PPU raised the STAT interrupt every block instead of on mode/LYC *transitions*, an
  interrupt storm that eventually corrupted control flow (pc landed in unrelated bank
  code). Fixed by edge-triggering STAT mode + LYC interrupts (`ppu.c`). After the fix
  boot runs with **no traps** and reaches the **main game loop**: the per-frame sound
  engine (`ParseMusic`/`GetMusicByte`/`GetFrequency`, bank 0x3a) runs every VBlank.
- 🔧 **Debug/trace tooling** added for this kind of work: execution trace ring +
  `gb_dbg_*` exports, and `tools/trace_boot.mjs` (boots the real ROM, runs to the
  first trap, prints the symbol-annotated block trace leading up to it).
- ⏳ **Next: black screen.** LCD is on (LCDC=0xe3) and the game loop runs, but the
  framebuffer is uniform black — investigate palette RAM population, BG tilemap/tile
  fetch (CGB VRAM banking), and whether the main loop is progressing past the
  intro/copyright. Then jump-table following to widen translated coverage cleanly.
- Practical note: translating all 128 banks → ~47 MB C / 24 MB wasm / ~4 min build.
  Targeted sets (e.g. `CODE_BANKS=0-7,58,66`) build in ~30s for fast iteration.
  Production will translate only code-bearing banks + jump-table-driven discovery.

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
