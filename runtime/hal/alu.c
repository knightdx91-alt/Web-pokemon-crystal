/* alu.c — ALU, accumulator rotates, DAA/CPL/SCF/CCF, 16-bit add, SP+e, and the
 * CB-prefix shift/rotate set (register + [hl] variants). Called by recompiler-
 * generated code (translate.py). Complete for the opcode set translate.py emits.
 */
#include "gb.h"
#include "hal_internal.h"

/* ----------------------------------------------------------------- 8-bit ALU */
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
uint8_t g_cp8[512]; uint32_t g_cp8_n;   /* debug: capture cpu.a at every `cp 8` */
void alu_cp(uint8_t v) {
    if (v == 8) g_cp8[g_cp8_n++ & 511] = cpu.a;
    SET_FLAG(FLAG_Z, cpu.a == v); SET_FLAG(FLAG_N, 1);
    SET_FLAG(FLAG_H, (cpu.a & 0xF) < (v & 0xF)); SET_FLAG(FLAG_C, cpu.a < v);
}
uint32_t gb_dbg_cp8_n(void) { return g_cp8_n; }
uint8_t  gb_dbg_cp8_at(uint32_t i) { return g_cp8[i & 511]; }

/* ------------------------------------------------------------ 16-bit / SP+e */
void add16_hl(uint16_t v) {
    uint32_t r = HL() + v;
    SET_FLAG(FLAG_N, 0);
    SET_FLAG(FLAG_H, ((HL() & 0x0FFF) + (v & 0x0FFF)) > 0x0FFF);
    SET_FLAG(FLAG_C, r > 0xFFFF);
    SET_HL((uint16_t)r);
}
uint16_t sp_offset(int e) {   /* used by add sp,e and ld hl,sp+e */
    uint16_t sp = cpu.sp;
    uint16_t r = (uint16_t)(sp + e);
    SET_FLAG(FLAG_Z, 0); SET_FLAG(FLAG_N, 0);
    SET_FLAG(FLAG_H, ((sp & 0xF) + (e & 0xF)) > 0xF);
    SET_FLAG(FLAG_C, ((sp & 0xFF) + (e & 0xFF)) > 0xFF);
    return r;
}

/* --------------------------------------------------- accumulator rotates etc */
void op_rlca(void) { uint8_t c = cpu.a >> 7; cpu.a = (uint8_t)((cpu.a << 1) | c); cpu.f = c ? FLAG_C : 0; }
void op_rrca(void) { uint8_t c = cpu.a & 1; cpu.a = (uint8_t)((cpu.a >> 1) | (c << 7)); cpu.f = c ? FLAG_C : 0; }
void op_rla(void)  { uint8_t c = GET_FLAG(FLAG_C) ? 1 : 0; uint8_t nc = cpu.a >> 7;
                     cpu.a = (uint8_t)((cpu.a << 1) | c); cpu.f = nc ? FLAG_C : 0; }
void op_rra(void)  { uint8_t c = GET_FLAG(FLAG_C) ? 1 : 0; uint8_t nc = cpu.a & 1;
                     cpu.a = (uint8_t)((cpu.a >> 1) | (c << 7)); cpu.f = nc ? FLAG_C : 0; }
void op_cpl(void)  { cpu.a = ~cpu.a; SET_FLAG(FLAG_N, 1); SET_FLAG(FLAG_H, 1); }
void op_scf(void)  { SET_FLAG(FLAG_N, 0); SET_FLAG(FLAG_H, 0); SET_FLAG(FLAG_C, 1); }
void op_ccf(void)  { SET_FLAG(FLAG_N, 0); SET_FLAG(FLAG_H, 0); SET_FLAG(FLAG_C, !GET_FLAG(FLAG_C)); }
void op_daa(void) {
    int a = cpu.a;
    if (!GET_FLAG(FLAG_N)) {
        if (GET_FLAG(FLAG_H) || (a & 0x0F) > 9) a += 0x06;
        if (GET_FLAG(FLAG_C) || a > 0x9F) { a += 0x60; SET_FLAG(FLAG_C, 1); }
    } else {
        if (GET_FLAG(FLAG_H)) a = (a - 6) & 0xFF;
        if (GET_FLAG(FLAG_C)) a -= 0x60;
    }
    cpu.a = (uint8_t)a;
    SET_FLAG(FLAG_Z, cpu.a == 0); SET_FLAG(FLAG_H, 0);
}

/* ------------------------------------------------------------- CB shift set */
/* Each returns the result and sets Z/N/H/C; the *_hl variants read/write [hl]. */
static uint8_t do_rlc(uint8_t v){ uint8_t c=v>>7; v=(uint8_t)((v<<1)|c); cpu.f=(v?0:FLAG_Z)|(c?FLAG_C:0); return v; }
static uint8_t do_rrc(uint8_t v){ uint8_t c=v&1; v=(uint8_t)((v>>1)|(c<<7)); cpu.f=(v?0:FLAG_Z)|(c?FLAG_C:0); return v; }
static uint8_t do_rl (uint8_t v){ uint8_t c=GET_FLAG(FLAG_C)?1:0,n=v>>7; v=(uint8_t)((v<<1)|c); cpu.f=(v?0:FLAG_Z)|(n?FLAG_C:0); return v; }
static uint8_t do_rr (uint8_t v){ uint8_t c=GET_FLAG(FLAG_C)?1:0,n=v&1; v=(uint8_t)((v>>1)|(c<<7)); cpu.f=(v?0:FLAG_Z)|(n?FLAG_C:0); return v; }
static uint8_t do_sla(uint8_t v){ uint8_t c=v>>7; v=(uint8_t)(v<<1); cpu.f=(v?0:FLAG_Z)|(c?FLAG_C:0); return v; }
static uint8_t do_sra(uint8_t v){ uint8_t c=v&1; v=(uint8_t)((v>>1)|(v&0x80)); cpu.f=(v?0:FLAG_Z)|(c?FLAG_C:0); return v; }
static uint8_t do_swap(uint8_t v){ v=(uint8_t)((v<<4)|(v>>4)); cpu.f=v?0:FLAG_Z; return v; }
static uint8_t do_srl(uint8_t v){ uint8_t c=v&1; v=(uint8_t)(v>>1); cpu.f=(v?0:FLAG_Z)|(c?FLAG_C:0); return v; }

#define CB(name, fn) \
  void cb_##name(uint8_t *r){ *r = fn(*r); } \
  void cb_##name##_hl(void){ bus_write(HL(), fn(bus_read(HL()))); }
CB(rlc, do_rlc) CB(rrc, do_rrc) CB(rl, do_rl) CB(rr, do_rr)
CB(sla, do_sla) CB(sra, do_sra) CB(swap, do_swap) CB(srl, do_srl)
#undef CB
