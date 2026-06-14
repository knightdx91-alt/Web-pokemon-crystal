/* hal_internal.h — internals shared between HAL modules and generated code.
 * Not part of the web-facing API (that's the gb_* exports in gb.h).
 */
#ifndef HAL_INTERNAL_H
#define HAL_INTERNAL_H

#include "gb.h"

/* stack helpers used by call/ret/rst translation */
static inline void push16(uint16_t v) {
    cpu.sp -= 2; bus_write(cpu.sp, v & 0xFF); bus_write(cpu.sp + 1, v >> 8);
}
static inline uint16_t pop16(void) {
    uint16_t v = bus_read(cpu.sp) | (bus_read(cpu.sp + 1) << 8);
    cpu.sp += 2; return v;
}

/* per-bank dispatch functions emitted by the recompiler (generated/bank_NN.c) */
void bank_00(uint16_t pc);
/* ... bank_01 .. bank_NN declared in generated/labels.h */

uint8_t joypad_read(void);  /* $FF00, runtime/hal/timer_irq_input.c */

/* set by hal_catch_up when the frame loop should reclaim control; the generated
 * dispatch checks it at each block boundary and returns (cpu.pc is checkpointed). */
extern int g_yield;

/* loud failure for un-translated addresses/opcodes (ARCHITECTURE.md §3.3) */
void trap(void);
void trap_pc(uint16_t pc);

/* ALU + helpers (runtime/hal/alu.c) operate on cpu.a / flags */
void alu_add(uint8_t v); void alu_adc(uint8_t v);
void alu_sub(uint8_t v); void alu_sbc(uint8_t v);
void alu_and(uint8_t v); void alu_or(uint8_t v);
void alu_xor(uint8_t v); void alu_cp(uint8_t v);

void add16_hl(uint16_t v);          /* add hl, rr */
uint16_t sp_offset(int e);          /* add sp,e / ld hl,sp+e (sets H/C) */

void op_rlca(void); void op_rrca(void); void op_rla(void); void op_rra(void);
void op_daa(void);  void op_cpl(void);  void op_scf(void); void op_ccf(void);
void cpu_stop(void);                /* stop: CGB speed switch, in gb.c */

/* CB shift/rotate set: register form (cb_x) + memory form (cb_x_hl) */
#define CB_DECL(n) void cb_##n(uint8_t *r); void cb_##n##_hl(void);
CB_DECL(rlc) CB_DECL(rrc) CB_DECL(rl) CB_DECL(rr)
CB_DECL(sla) CB_DECL(sra) CB_DECL(swap) CB_DECL(srl)
#undef CB_DECL

#endif /* HAL_INTERNAL_H */
