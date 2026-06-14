/* testbus.c — flat 64KB memory + CPU state harness for the SingleStepTests driver.
 * Links with the generated optest_gen.c and the real runtime/hal/alu.c so the tests
 * exercise the actual recompiler semantics.
 */
#include "gb.h"
#include "hal_internal.h"

CpuState cpu;
static uint8_t ram[0x10000];

uint8_t bus_read(uint16_t addr) { return ram[addr]; }
void    bus_write(uint16_t addr, uint8_t value) { ram[addr] = value; }

/* stubs for symbols the translated bodies may reference */
void trap(void) {}
void trap_pc(uint16_t pc) { (void)pc; }
void cpu_stop(void) {}

void exec_op(void);   /* generated */

/* ---- exports for the Node driver ---- */
void set_state(uint8_t a, uint8_t f, uint8_t b, uint8_t c, uint8_t d, uint8_t e,
               uint8_t h, uint8_t l, uint16_t sp, uint16_t pc) {
    cpu.a = a; cpu.f = f & 0xF0; cpu.b = b; cpu.c = c; cpu.d = d; cpu.e = e;
    cpu.h = h; cpu.l = l; cpu.sp = sp; cpu.pc = pc;
}
void     wr(uint16_t addr, uint8_t v) { ram[addr] = v; }
uint8_t  rd(uint16_t addr) { return ram[addr]; }
void     step(void) { exec_op(); }

uint8_t  g_a(void) { return cpu.a; }  uint8_t g_f(void) { return cpu.f; }
uint8_t  g_b(void) { return cpu.b; }  uint8_t g_c(void) { return cpu.c; }
uint8_t  g_d(void) { return cpu.d; }  uint8_t g_e(void) { return cpu.e; }
uint8_t  g_h(void) { return cpu.h; }  uint8_t g_l(void) { return cpu.l; }
uint16_t g_sp(void) { return cpu.sp; } uint16_t g_pc(void) { return cpu.pc; }
