/* alu.c — 8-bit ALU + CB rotate/shift helpers operating on cpu.a / flags.
 * Called by recompiler-generated code (translate.py). These few are implemented
 * because they are exercised by nearly every translated block; the rest follow the
 * same pattern.
 */
#include "gb.h"
#include "hal_internal.h"

void alu_add(uint8_t v) {
    uint16_t r = cpu.a + v;
    SET_FLAG(FLAG_H, ((cpu.a & 0xF) + (v & 0xF)) > 0xF);
    SET_FLAG(FLAG_C, r > 0xFF);
    cpu.a = (uint8_t)r;
    SET_FLAG(FLAG_Z, cpu.a == 0); SET_FLAG(FLAG_N, 0);
}
void alu_adc(uint8_t v) {
    uint8_t c = GET_FLAG(FLAG_C) ? 1 : 0;
    uint16_t r = cpu.a + v + c;
    SET_FLAG(FLAG_H, ((cpu.a & 0xF) + (v & 0xF) + c) > 0xF);
    SET_FLAG(FLAG_C, r > 0xFF);
    cpu.a = (uint8_t)r;
    SET_FLAG(FLAG_Z, cpu.a == 0); SET_FLAG(FLAG_N, 0);
}
void alu_sub(uint8_t v) {
    SET_FLAG(FLAG_H, (cpu.a & 0xF) < (v & 0xF));
    SET_FLAG(FLAG_C, cpu.a < v);
    cpu.a = cpu.a - v;
    SET_FLAG(FLAG_Z, cpu.a == 0); SET_FLAG(FLAG_N, 1);
}
void alu_sbc(uint8_t v) {
    uint8_t c = GET_FLAG(FLAG_C) ? 1 : 0;
    int r = cpu.a - v - c;
    SET_FLAG(FLAG_H, ((cpu.a & 0xF) - (v & 0xF) - c) < 0);
    SET_FLAG(FLAG_C, r < 0);
    cpu.a = (uint8_t)r;
    SET_FLAG(FLAG_Z, cpu.a == 0); SET_FLAG(FLAG_N, 1);
}
void alu_and(uint8_t v) { cpu.a &= v; cpu.f = (cpu.a == 0 ? FLAG_Z : 0) | FLAG_H; }
void alu_or(uint8_t v)  { cpu.a |= v; cpu.f = (cpu.a == 0 ? FLAG_Z : 0); }
void alu_xor(uint8_t v) { cpu.a ^= v; cpu.f = (cpu.a == 0 ? FLAG_Z : 0); }
void alu_cp(uint8_t v) {
    SET_FLAG(FLAG_Z, cpu.a == v); SET_FLAG(FLAG_N, 1);
    SET_FLAG(FLAG_H, (cpu.a & 0xF) < (v & 0xF)); SET_FLAG(FLAG_C, cpu.a < v);
}

/* CB helpers — TODO: rrc/rl/rr/sla/sra/swap/srl and the _hl variants. */
void cb_rlc(uint8_t *r) {
    uint8_t c = *r >> 7; *r = (uint8_t)((*r << 1) | c);
    cpu.f = (*r == 0 ? FLAG_Z : 0) | (c ? FLAG_C : 0);
}
