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

/* loud failure for un-translated addresses/opcodes (ARCHITECTURE.md §3.3) */
void trap(void);
void trap_pc(uint16_t pc);

/* ALU + CB helpers (runtime/hal/alu.c) operate on cpu.a / flags */
void alu_add(uint8_t v); void alu_adc(uint8_t v);
void alu_sub(uint8_t v); void alu_sbc(uint8_t v);
void alu_and(uint8_t v); void alu_or(uint8_t v);
void alu_xor(uint8_t v); void alu_cp(uint8_t v);
void cb_rlc(uint8_t *r); void cb_rrc(uint8_t *r);
void cb_rl(uint8_t *r);  void cb_rr(uint8_t *r);
void cb_sla(uint8_t *r); void cb_sra(uint8_t *r);
void cb_swap(uint8_t *r);void cb_srl(uint8_t *r);
void cb_rlc_hl(void); /* …_hl variants act on bus_read/write(HL()) */

#endif /* HAL_INTERNAL_H */
