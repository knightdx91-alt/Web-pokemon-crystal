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

void gb_run_frame(void) {
    /* Run translated code, advancing the HAL, until the PPU signals VBlank.
     * The recompiler entry bank_00 dispatches from cpu.pc; banked calls re-enter
     * via rom_call(). Here we drive one frame's worth of execution. */
    int start_ly = io[0x44];
    (void)start_ly;
    /* TODO: loop:
     *   int_service();
     *   if (!cpu.halted) bank_dispatch(cpu.pc);   // runs a block, updates cpu.cycles
     *   uint32_t dt = consume_cycle_delta();
     *   ppu_step(dt); apu_step(dt); timer_step(dt);
     *   until LY wraps to 144 (VBlank).
     */
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
