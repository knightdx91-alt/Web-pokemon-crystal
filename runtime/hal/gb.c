/* gb.c — WASM entrypoints + the frame loop (the only surface web/ sees).
 * STATUS: skeleton wiring. Runs translated code until VBlank, ticking the HAL.
 */
#include "gb.h"
#include "hal_internal.h"

CpuState cpu;
const uint8_t *g_rom;
uint32_t g_rom_len;

extern uint8_t io[];
extern uint8_t ie_reg;

static void trace_push(uint32_t v);   /* execution trace ring (defined below) */

uint32_t g_traps;
uint16_t g_last_trap_pc;
uint8_t  g_last_trap_bank;

void cpu_stop(void)        { /* TODO: CGB double-speed switch via KEY1 ($FF4D) */ }
void trap(void)            { g_traps++; }
void trap_pc(uint16_t pc)  { g_traps++; g_last_trap_pc = pc; g_last_trap_bank = cpu.rom_bank;
                             cpu.halted = 1; /* stop cleanly: pc didn't advance here */ }

/* Execution entered HRAM ($FF80-$FFFE). The only code pokecrystal runs from HRAM is
 * the OAM-DMA routine (hTransferShadowOAM): write the source page to rDMA, busy-wait,
 * ret. It can't be statically recompiled (RAM code), so emulate it directly from the
 * bytes the game copied there. The DMA effect happens on the rDMA ($FF46) write. */
void hram_exec(uint16_t pc) {
    if (pc == 0xFF80) {
        uint8_t page = bus_read(0xFF81);   /* immediate of `ld a, HIGH(wShadowOAM)` */
        bus_write(0xFF46, page);           /* perform the OAM DMA copy */
        cpu.a = 0;                         /* a == 0 after the dec-to-zero wait loop */
        cpu.cycles += 160;                 /* routine duration (approx) */
        cpu.pc = pop16();                  /* ret */
        return;
    }
    trap_pc(pc);                           /* any other HRAM execution is unexpected */
}

/* --- execution trace ring (last N dispatched blocks, bank<<16|pc) ---------- */
#define TRACE_N 4096
static uint32_t g_trace[TRACE_N];     /* bank<<16 | pc */
static uint32_t g_trace_aux[TRACE_N]; /* wCurChannel<<8 | cpu.f, sampled per block */
static uint32_t g_trace_i;
static void trace_push(uint32_t v) {
    g_trace[g_trace_i & (TRACE_N - 1)] = v;
    g_trace_aux[g_trace_i & (TRACE_N - 1)] = (bus_read(0xC299) << 8) | cpu.f;
    g_trace_i++;
}
uint32_t gb_dbg_trace_head(void) { return g_trace_i; }
uint32_t gb_dbg_trace_at(uint32_t i) { return g_trace[i & (TRACE_N - 1)]; }
uint32_t gb_dbg_trace_aux(uint32_t i) { return g_trace_aux[i & (TRACE_N - 1)]; }

/* --- debug surface (read CPU/trap state from JS) --------------------------- */
int      gb_dbg_io(int a)     { return io[a & 0x7f]; }
int      gb_dbg_read(int a)   { return bus_read((uint16_t)a); }
uint16_t gb_dbg_pc(void)      { return cpu.pc; }
uint8_t  gb_dbg_bank(void)    { return cpu.rom_bank; }
uint8_t  gb_dbg_halted(void)  { return (uint8_t)cpu.halted; }
uint8_t  gb_dbg_lcdc(void)    { return io[0x40]; }
uint32_t gb_dbg_traps(void)   { return g_traps; }
uint16_t gb_dbg_trap_pc(void) { return g_last_trap_pc; }
uint8_t  gb_dbg_trap_bank(void){ return g_last_trap_bank; }

void gb_init(const uint8_t *rom, uint32_t rom_len) {
    g_rom = rom; g_rom_len = rom_len;
    cpu = (CpuState){0};
    cpu.pc = 0x0100;          /* post-boot entry */
    cpu.sp = 0xFFFE;
    cpu.rom_bank = 1;
    cpu.a = 0x11;             /* CGB: A=0x11 at boot handoff */

    /* Post-boot hardware state, as the CGB boot ROM leaves it. Crucially the LCD
     * is ON and the PPU is running (LY counting) — Init.wait polls rLY for VBlank
     * before it turns the LCD off, so a zeroed LCDC would deadlock the boot. */
    io[0x40] = 0x91;          /* LCDC: LCD on, BG on, tile data $8000 */
    io[0x41] = 0x85;          /* STAT */
    io[0x47] = 0xFC;          /* BGP (DMG-compat) */
    io[0x48] = 0xFF;          /* OBP0 */
    io[0x49] = 0xFF;          /* OBP1 */
    ie_reg   = 0x00;
}

/* ------------------------------------------------------------------ HAL sync */
/* Advance the PPU/APU/timers to the CPU's current cycle count. Called from the
 * generated dispatch (emit.py) at every block boundary, and from the frame loop.
 * Device-only: it never services interrupts (that must happen at a clean top-level
 * boundary, not mid-block). It raises g_yield when the frame is done or the
 * per-frame cycle deadline passes, which makes the generated dispatch return so
 * the frame loop regains control (the emulated call stack persists in cpu.sp). */
static uint64_t synced;
int g_yield;
static uint64_t g_deadline;

int ppu_frame_pending(void);   /* ppu.c */

void hal_catch_up(void) {
    uint64_t now = cpu.cycles;
    if (now > synced) {
        uint32_t dt = (uint32_t)(now - synced);
        synced = now;
        ppu_step(dt);
        apu_step(dt);
        timer_step(dt);
    }
    /* Yield only on the full-frame cycle deadline, NOT the instant LY hits 144.
     * Running a whole frame's cycles per gb_run_frame means the VBlank interrupt is
     * serviced WITHIN the frame while LY is still ~144 — which the game relies on
     * (e.g. Serve2bppRequest only serves during LY 144-145). Yielding at the VBlank
     * latch instead pushed the handler into the next call at a drifted LY. */
    if (cpu.cycles >= g_deadline)
        g_yield = 1;
}

/* Run one frame: drive translated code (servicing interrupts at top level and
 * honouring HALT) until the PPU latches a frame, or a one-frame cycle deadline
 * passes (covers the LCD-off case where no VBlank is produced). */
void gb_run_frame(void) {
    g_yield = 0;
    g_deadline = cpu.cycles + 70224;        /* T-cycles per frame */

    uint32_t guard = 0;                     /* hard cap: never spin a frame forever */
    while (!g_yield && guard++ < 4000000) {
        if (cpu.ime && int_pending()) {
            cpu.halted = 0;
            int_service();                  /* push pc, jump to vector, clear IME */
        }
        if (cpu.halted) {
            cpu.cycles += 4;                /* idle: tick hardware to raise the IRQ */
            hal_catch_up();
            if (int_pending()) cpu.halted = 0;
        } else {
            trace_push((uint32_t)cpu.rom_bank << 16 | cpu.pc);
            rom_dispatch(cpu.pc);           /* run exactly one block, advance cpu.pc */
            hal_catch_up();
        }
    }
    ppu_take_frame();                       /* consume the frame latch */
}

