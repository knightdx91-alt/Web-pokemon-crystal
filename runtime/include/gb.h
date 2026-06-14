/* gb.h — the contract shared by recompiler-generated code and the HAL.
 *
 * Generated code (generated/rom_code.c) calls bus_read/bus_write and the helpers
 * here; it never touches hardware directly. The HAL (runtime/hal source) implements
 * the bus + devices. The web frontend only sees the gb_* exports at the bottom.
 *
 * See ARCHITECTURE.md §3-§4.
 */
#ifndef GB_H
#define GB_H

#include <stdint.h>

/* ------------------------------------------------------------------ CPU state */

typedef struct {
    uint8_t  a, f, b, c, d, e, h, l;  /* f: Z=bit7 N=bit6 H=bit5 C=bit4 */
    uint16_t sp, pc;
    uint8_t  rom_bank, ram_bank;      /* MBC3 mirror; bus.c is authoritative */
    int      ime;                     /* interrupt master enable */
    int      halted;
    uint64_t cycles;                  /* T-cycles since reset; drives HAL catch-up */
} CpuState;

extern CpuState cpu;

/* 16-bit register pair accessors */
#define AF() ((uint16_t)((cpu.a << 8) | cpu.f))
#define BC() ((uint16_t)((cpu.b << 8) | cpu.c))
#define DE() ((uint16_t)((cpu.d << 8) | cpu.e))
#define HL() ((uint16_t)((cpu.h << 8) | cpu.l))
#define SET_AF(v) do { cpu.a = (uint8_t)((v) >> 8); cpu.f = (uint8_t)((v) & 0xF0); } while (0)
#define SET_BC(v) do { cpu.b = (uint8_t)((v) >> 8); cpu.c = (uint8_t)(v); } while (0)
#define SET_DE(v) do { cpu.d = (uint8_t)((v) >> 8); cpu.e = (uint8_t)(v); } while (0)
#define SET_HL(v) do { cpu.h = (uint8_t)((v) >> 8); cpu.l = (uint8_t)(v); } while (0)

/* flag bits */
#define FLAG_Z 0x80
#define FLAG_N 0x40
#define FLAG_H 0x20
#define FLAG_C 0x10
#define SET_FLAG(bit, on) do { if (on) cpu.f |= (bit); else cpu.f &= ~(bit); } while (0)
#define GET_FLAG(bit) ((cpu.f & (bit)) != 0)

/* ------------------------------------------------------------------ memory bus */
/* The single choke point. Generated code uses only these for memory access. */

uint8_t bus_read(uint16_t addr);
void    bus_write(uint16_t addr, uint8_t value);

/* MBC3 banking, invoked by bus_write to the ROM/RAM control ranges. */
void    mbc_write(uint16_t addr, uint8_t value);

/* ------------------------------------------------------------------ devices    */

void ppu_step(uint32_t tcycles);      /* advance PPU, may raise STAT/VBlank IRQ   */
void apu_step(uint32_t tcycles);
void timer_step(uint32_t tcycles);
int  ppu_take_frame(void);            /* 1 once per frame when LY reaches 144     */

void hal_catch_up(void);              /* advance devices to cpu.cycles (no IRQ svc)*/

void int_request(uint8_t mask);       /* OR into IF */
int  int_pending(void);               /* IE & IF & 0x1F */
void int_service(void);               /* push PC, jump to vector, clear IME       */

/* ------------------------------------------------------------------ banked call */
/* Trampoline for farcall/rst/homecall: run code at (bank, addr) and return.
 * Implemented in generated dispatch; declared here so the HAL/vectors can use it. */
void rom_call(uint8_t bank, uint16_t addr);

/* Entry the recompiler emits: runs translated code starting at pc until it
 * returns to the dispatcher (e.g. on ret to a sentinel). */
void rom_exec(void);

/* ------------------------------------------------------------------ exports     */
/* The only surface the web/ frontend sees. */

void            gb_init(const uint8_t *rom, uint32_t rom_len);
void            gb_run_frame(void);          /* run until next VBlank */
void            gb_set_buttons(uint8_t mask); /* bit0 A bit1 B bit2 Sel bit3 Start
                                                 bit4 R bit5 L bit6 U bit7 D */
const uint8_t  *gb_framebuffer(void);        /* 160*144*4 RGBA */
const int16_t  *gb_audio_samples(void);      /* interleaved stereo PCM ring */
uint32_t        gb_audio_available(void);    /* sample frames ready */
void            gb_audio_consume(uint32_t frames);

#define GB_SCREEN_W 160
#define GB_SCREEN_H 144

#endif /* GB_H */
