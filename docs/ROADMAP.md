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
- 🎉🎉🎉 **RENDERS IN COLOR.** The real Pokémon Crystal ROM, statically recompiled to
  WebAssembly, now boots and renders the intro **in color**, running stably for
  16,000+ frames with zero traps. Milestone reached via a chain of fixes this session
  (newest first):
  1. **CGB palette address bug** — `bus.c` passed the full address (`0xFF69`) to
     `ppu_pal_write/read`, which switch on the low byte (`0x69`); no case matched, so
     palette RAM was never written. Fix: `addr & 0xFF`. → palettes load, color renders.
  2. **Cycle inflation** — emitter charged whole-block cycles up front; early
     `ret z`/`jr` over-charged → blew the VBlank `Serve2bppRequest` LY-window → tiles
     never loaded. Fix: charge cycles at each exit point. → tiles load, intro draws.
  3. **Conditional `ret`** — disassembler dropped the fall-through after `ret z/nz/..`;
     the C switch silently skipped instructions. Fix: conditional ret continues inline.
     → boots into the intro, sound works, no traps.
  4. Frame model yields on a full-frame cycle deadline (VBlank handler runs near LY=144).
- Reusable tooling: `tools/render_bg.mjs` (grayscale BG dump), framebuffer PNG capture,
  trace ring + `gb_dbg_*` debug surface (read/watch+PC, serve/preamble/palette counters).
- ⏳ **Next:** reach/verify the title screen + main menu (drive input); audio output
  to WebAudio; wire the web/ frontend to load a local ROM; then save (SRAM) + perf.
  Also: trim the per-block debug probes in `trace_push` once timing work is settled.
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
- ✅✅✅ **MAJOR FIX #2 (cycle accuracy) — intro graphics load.** The conditional-ret
  fix made blocks include the post-ret tail, but the emitter charged the whole block's
  cycles up front, so an early `ret z`/`jr` over-charged for code that never ran
  (~47% over on the VBlank preamble). That pushed `Serve2bppRequest` past its tight
  `LY 144-145` window, so tiles never loaded. `emit.py` now charges cycles at each
  EXIT point (`_unconditional_exit` handles a conditional ret/call as the last insn).
  Result: VBlank preamble ~1016→716 cycles, `Serve2bppRequest` hits LY=145 and copies,
  **tiles load (vramTiles 0→3000+), BG map fills, the 2bpp handshake completes**. The
  BG tilemap renders real structured content (`tools/render_bg.mjs` → grayscale PNG).
- ⏳ **Current blocker — CGB palettes never written → black screen.** The game loads
  tiles and animates the intro (LCDC window toggling) but writes the CGB palette RAM
  **zero times** (`gb_dbg_palw_bg/_obj = 0`), and `hCGBPalUpdate` stays 0, so
  `ForceUpdateCGBPals` never copies `wBGPals2`→`rBGPD`. With palette RAM all zero the
  renderer (CGB path) shows black even though the tile/map content is correct. Next:
  find where the intro loads palettes (and sets `hCGBPalUpdate`) and why that code
  doesn't run — likely another divergence, OR a pre-fade phase. Grayscale render
  confirms the content is there; only color is missing.
- ✅✅ **MAJOR FIX #1 (conditional ret) — boots into the intro now.** Root-caused via a
  per-block `wCurChannel`/`cpu.f` step trace (`gb_dbg_trace_aux`): the disassembler
  treated a CONDITIONAL `ret z/nz/nc/c` like an unconditional one — it ended the
  block without disassembling the fall-through, and the emitter's C `switch` then
  fell through to the next case, SILENTLY SKIPPING the instructions between the
  conditional ret and the next block. `_UpdateSound` (`3a:405c`) is exactly
  `... ret z; xor a; ld [wCurChannel],a; ...`; the reset after `ret z` was dropped,
  so the sound channel loop overran (the whole-session black-screen bug).
  - Fix in `disasm.py`: only UNCONDITIONAL ret/reti ends a block; conditional ret
    continues inline (emit.py already emits the guarded return).
  - Result on the real ROM: **zero traps, sound runs cleanly once/frame, the boot
    reaches the intro CUTSCENE** (hVBlank cutscene path), runs the graphics-request
    system (HDMA fires, BG tilemap written).
- 🛠 **Frame-timing improved:** `gb_run_frame` now yields on a full-frame cycle
  deadline (70224 T-cycles) instead of the instant LY hits 144, so the VBlank handler
  runs WITHIN the frame near LY=144 (the game relies on this).
- ⏳ **Current blocker — VBlank interrupt-timing drift.** Boot waits in
  `Request2bpp.wait` for `wRequested2bppSize→0`, cleared by `Serve2bppRequest` in the
  VBlank handler — but that only serves during `LY 144-145`, and we measure it
  entering at **LY=146** (`gb_dbg_serve_ly`), so it returns early and never copies
  (`gb_dbg_serve_copies=0`). The handler reaches `Serve2bppRequest` ~2 scanlines late
  because our per-block stepping services the VBlank IRQ a bit after LY=144 and the
  preamble adds more. Fix = tighter interrupt-timing fidelity (service VBlank closer
  to the LY 143→144 edge / finer PPU stepping during the handler).
- 🧪 **Earlier verified facts (debug: `gb_dbg_cp8_*`, predecessor tracing):**
  - The loop terminator `cp 8; jp nz` at `3a:410f` is correct: when it sees `a==8`
    it ALWAYS falls through (exits) — `cpu.f=0xC0` (Z set), `JUMPED=0 EXITED=1`.
  - `3a:406b` (loop body) is entered ONLY from `3a:4102` — there is NO wild jump in.
  - Yet `wCurChannel` still climbs 0→14 in a single `_UpdateSound` invocation
    (`cp 8` is called only ~15 times TOTAL, monotonically 1..14), and only ONE channel
    is briefly on (`40fc _UpdateSound.sound_channel_on` appears once as a `4102` pred).
  - This is an unresolved contradiction under the static model: the loop should stop
    at `a==8` but reaches 14. Resolving it needs WASM-level inspection of the compiled
    `4102` block, or step-granular CPU-state capture across the `a==8` iteration
    (instrument the branch to log `wCurChannel`+`cpu.f`+`cpu.pc` every pass).
- ⏳ **Strategic next regardless:** code/data separation / jump-table following in the
  recursive disassembler (mis-decoded data still produces spurious blocks like the
  `call nc,$4111` at `3a:6337`). It's the general gate to the title screen.
  (`gb_dbg_f/_a` are generic debug slots; harmless in normal builds.)
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
