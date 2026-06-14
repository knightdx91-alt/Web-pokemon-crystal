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
- ⏳ **Black screen root-caused to the SOUND ENGINE.** Diagnostics added: VRAM-write
  + HDMA counters, IO/palette/VRAM accessors. Findings: VRAM is written only during
  the init clear (16384 bytes) then never again; **no graphics load**. The full
  execution-trace ring shows the last 1024 dispatched blocks are *all* in the sound
  bank (0x3a) — i.e. the VBlank handler's `_UpdateSound` never returns. It's stuck
  endlessly parsing music commands (ParseMusic/GetMusicByte/GetFrequency), starving
  the main thread so it never loads the intro/title graphics. Likely a corrupted
  music pointer / duration counter from a subtle instruction mistranslation.
  - Implemented CGB **HDMA/VDMA** ($FF51-$FF55) + **OAM DMA** so graphics CAN load
    once the sound loop is fixed.
- ✅ **Instruction semantics VALIDATED** against SingleStepTests (sm83), `make optest`
  (`tools/optest/`): a generated single-instruction executor reuses the real
  `translate.py` (immediates-from-memory mode), compiled to wasm, run over all
  256 base + 256 CB opcodes × 1000 vectors each. Result: **every defined opcode
  passes** (fixed STOP to a 1-byte advance). This conclusively rules out instruction
  mistranslation — the sound-engine loop is a **HAL / timing / banking** issue, not a
  bad opcode. Suspects: interrupt/IME timing, ROM-bank state during the banked music
  read (`GetMusicByte`), or a hardware register the engine polls.
- 🔬 **Sound loop narrowed precisely** (via `gb_dbg_read` WRAM accessor): the main
  thread is permanently halted in `DelayFrame` because the VBlank handler's
  `_UpdateSound` never clears `wVBlankOccurred` — it never returns. State dump:
  `wMusicPlaying=1` but all 8 channel structs are zero, and `wCurChannel=14` — the
  per-channel loop (`cp NUM_CHANNELS; jp nz, .loop`, bound = 8) has **overrun past
  channel 8** and is parsing uninitialized WRAM as a music channel (addr 0 → reads
  ROM/garbage), looping forever in ParseMusic. Opcodes are proven correct, so this is
  state corruption / timing, not a bad instruction. Inconsistency (`wMusicPlaying=1`
  with empty channels) points at the music-start path or an interrupt/IME-timing
  issue corrupting `wCurChannel`/channel flags.
- 🎯 **Mechanism pinned** (write-watch tooling `gb_dbg_set_watch`/`_watch_at`): the
  per-channel loop runs `wCurChannel` 0→14 (`0,0,1..e`) then gets STUCK inside
  `ParseMusic` for one channel. Ordered trace of the hang:
  `ParseMusic → ParseMusicCommand → Music_PitchSlide → GetMusicByte → GetFrequency`
  repeating forever. That channel is **ON but its music address is 0**, so
  `GetMusicByte`/`_LoadMusicByte` read ROM `$0000` (header/vectors) and parse it as an
  endless stream of pitch-slide commands — never a note, so `ParseMusic` never returns
  and the VBlank handler never finishes. `wMusicID=0` (no real song) yet
  `wMusicPlaying=1` and a channel enabled = inconsistent state. The loop terminator
  (`cp NUM_CHANNELS; jp nz` at `3a:410f`) and its generated C are verified correct, so
  the bug is upstream: a channel got `SOUND_CHANNEL_ON` set without a valid pointer.
- 🧩 **Refined to TWO coupled causes** (debug: `gb_dbg_set_watch`, `gb_dbg_cp8_*`):
  - The STOP-length fix (optest work) corrected bank-0x3a disassembly: for the first
    ~30 frames (channels off) the channel loop now stops cleanly at 8 (verified: cp-8
    sees `cpu.a` 1..8, and pc rests at `DelayFrame.halt`).
  - But once the game enables a channel, it has **music address = 0** (the music-start
    path didn't populate the pointer) → `_LoadMusicByte` reads ROM `$0000` as garbage
    music. Parsing that garbage flows through **mis-decoded data regions** (e.g. a
    spurious `call nc,$4111` at `3a:6337` from data decoded as code) that re-enter the
    loop terminator **without the `cp`**, so `wCurChannel` overruns to 14 and sticks.
- ⏳ **Next (two threads):** (1) trace the music-start (`PlayMusic`/`LoadChannel`/song
  header read + its bank switch) to find why the channel pointer stays 0; (2)
  jump-table / code-data separation so garbage data isn't decoded as code and can't
  create spurious entry points. Either alone may unblock boot; both are needed for
  correctness.
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
