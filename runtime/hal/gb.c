/* gb.c — WASM entrypoints + the frame loop (the only surface web/ sees).
 * STATUS: skeleton wiring. Runs translated code until VBlank, ticking the HAL.
 */
#include "gb.h"
#include "hal_internal.h"

CpuState cpu;
const uint8_t *g_rom;
uint32_t g_rom_len;

extern uint8_t io[];

void cpu_stop(void)        { /* TODO: CGB double-speed switch via KEY1 ($FF4D) */ }
void trap(void)            { /* TODO: surface un-translated opcode to JS console */ }
void trap_pc(uint16_t pc)  { (void)pc; /* TODO: un-translated address */ }

void gb_init(const uint8_t *rom, uint32_t rom_len) {
    g_rom = rom; g_rom_len = rom_len;
    cpu = (CpuState){0};
    cpu.pc = 0x0100;          /* post-boot entry */
    cpu.sp = 0xFFFE;
    cpu.rom_bank = 1;
    cpu.a = 0x11;             /* CGB: A=0x11 at boot handoff */
    /* TODO: full post-BIOS register/IO state for CGB. */
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
    if (ppu_frame_pending() || cpu.cycles >= g_deadline)
        g_yield = 1;
}

/* Run one frame: drive translated code (servicing interrupts at top level and
 * honouring HALT) until the PPU latches a frame, or a one-frame cycle deadline
 * passes (covers the LCD-off case where no VBlank is produced). */
void gb_run_frame(void) {
    g_yield = 0;
    g_deadline = cpu.cycles + 70224;        /* T-cycles per frame */

    while (!g_yield) {
        if (cpu.ime && int_pending()) {
            cpu.halted = 0;
            int_service();                  /* push pc, jump to vector, clear IME */
        }
        if (cpu.halted) {
            cpu.cycles += 4;                /* idle: tick hardware to raise the IRQ */
            hal_catch_up();
            if (int_pending()) cpu.halted = 0;
        } else {
            rom_exec();                     /* runs until ret-to-top, halt, or yield */
            hal_catch_up();
        }
    }
    ppu_take_frame();                       /* consume the frame latch */
}

void rom_call(uint8_t bank, uint16_t addr) {
    /* Trampoline: switch ROM bank, dispatch, restore. Wired once banks are emitted. */
    uint8_t prev = cpu.rom_bank;
    cpu.rom_bank = bank;
    cpu.pc = addr;
    bank_00(addr);   /* TODO: dispatch to bank_<bank> via generated table */
    cpu.rom_bank = prev;
}

void rom_exec(void) { bank_00(cpu.pc); }
