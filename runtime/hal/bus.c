/* bus.c — Game Boy memory map + MBC3 banking (ARCHITECTURE.md §4).
 * The single choke point every generated memory access flows through.
 *
 * STATUS: skeleton. Routing is laid out; device-register reads/writes and full
 * MBC3 (RTC) behaviour are TODO.
 */
#include "gb.h"
#include "hal_internal.h"

/* ROM banks are emitted by the recompiler as data arrays (rom_bank_NN) for data
 * banks; code banks live as functions. For the bus we need the raw bytes of every
 * bank, so the build also links a flat rom image. */
extern const uint8_t *g_rom;       /* set by gb_init */
extern uint32_t       g_rom_len;

uint8_t vram[0x4000];              /* 2 banks (CGB) */
uint8_t wram[0x8000];              /* 8 banks (CGB) */
uint8_t oam[0xA0];
uint8_t hram[0x80];
uint8_t io[0x80];                  /* $FF00-$FF7F */
uint8_t ie_reg;                    /* $FFFF */

static uint8_t cart_ram[0x8000];
static int vram_bank = 0;
static int wram_bank = 1;

const uint8_t *gb_dbg_vram(void) { return vram; }   /* debug: VRAM base offset */
uint32_t g_vramw, g_hdma;                            /* debug counters */
uint32_t gb_dbg_vramw(void) { return g_vramw; }
uint32_t gb_dbg_hdma(void)  { return g_hdma; }

/* write-watch: record values written to a chosen address (debug) */
static int g_watch = -1;
static uint8_t g_watch_buf[1024];
static uint32_t g_watch_head;
void     gb_dbg_set_watch(int addr) { g_watch = addr; g_watch_head = 0; }
uint32_t gb_dbg_watch_head(void) { return g_watch_head; }
uint8_t  gb_dbg_watch_at(uint32_t i) { return g_watch_buf[i & 1023]; }

uint8_t bus_read(uint16_t addr) {
    if (addr < 0x4000)            return g_rom[addr];                       /* bank 0 */
    if (addr < 0x8000)            return g_rom[cpu.rom_bank * 0x4000 + (addr - 0x4000)];
    if (addr < 0xA000)            return vram[vram_bank * 0x2000 + (addr - 0x8000)];
    if (addr < 0xC000)            return cart_ram[cpu.ram_bank * 0x2000 + (addr - 0xA000)];
    if (addr < 0xD000)            return wram[addr - 0xC000];
    if (addr < 0xE000)            return wram[wram_bank * 0x1000 + (addr - 0xD000)];
    if (addr < 0xFE00)            return wram[addr - 0xE000];               /* echo */
    if (addr < 0xFEA0)            return oam[addr - 0xFE00];
    if (addr < 0xFF00)            return 0xFF;                              /* unusable */
    if (addr == 0xFF00)           return joypad_read();                    /* input.c */
    if (addr >= 0xFF68 && addr <= 0xFF6B) return ppu_pal_read(addr);       /* CGB palettes */
    if (addr < 0xFF80)            return io[addr - 0xFF00];                 /* TODO device regs */
    if (addr < 0xFFFF)            return hram[addr - 0xFF80];
    return ie_reg;
}

void bus_write(uint16_t addr, uint8_t value) {
    if ((int)addr == g_watch) g_watch_buf[g_watch_head++ & 1023] = value;
    if (addr < 0x8000)            { mbc_write(addr, value); return; }       /* MBC control */
    if (addr < 0xA000)            { g_vramw++; vram[vram_bank * 0x2000 + (addr - 0x8000)] = value; return; }
    if (addr < 0xC000)            { cart_ram[cpu.ram_bank * 0x2000 + (addr - 0xA000)] = value; return; }
    if (addr < 0xD000)            { wram[addr - 0xC000] = value; return; }
    if (addr < 0xE000)            { wram[wram_bank * 0x1000 + (addr - 0xD000)] = value; return; }
    if (addr < 0xFE00)            { wram[addr - 0xE000] = value; return; }
    if (addr < 0xFEA0)            { oam[addr - 0xFE00] = value; return; }
    if (addr < 0xFF00)            return;
    if (addr == 0xFF46)           { uint16_t s = (uint16_t)(value << 8);     /* OAM DMA */
                                    for (int i = 0; i < 0xA0; i++) oam[i] = bus_read(s + i);
                                    io[0x46] = value; return; }
    if (addr == 0xFF4F)           { vram_bank = value & 1; return; }
    if (addr == 0xFF55)           {                                          /* CGB VDMA/HDMA */
        uint16_t src = (uint16_t)(((io[0x51] << 8) | io[0x52]) & 0xFFF0);
        uint16_t dst = (uint16_t)(((io[0x53] << 8) | io[0x54]) & 0x1FF0);     /* VRAM offset */
        int len = ((value & 0x7F) + 1) * 0x10;
        for (int i = 0; i < len; i++)
            vram[vram_bank * 0x2000 + ((dst + i) & 0x1FFF)] = bus_read((uint16_t)(src + i));
        g_hdma++; io[0x55] = 0xFF;   /* transfer complete */
        return;
    }
    if (addr >= 0xFF68 && addr <= 0xFF6B) { ppu_pal_write(addr, value); return; }  /* CGB palettes */
    if (addr == 0xFF70)           { wram_bank = (value & 7) ? (value & 7) : 1; return; }
    if (addr < 0xFF80)            { io[addr - 0xFF00] = value; return; }    /* TODO device regs */
    if (addr < 0xFFFF)            { hram[addr - 0xFF80] = value; return; }
    ie_reg = value;
}

void mbc_write(uint16_t addr, uint8_t value) {
    /* MBC3: $2000-$3FFF rom bank (7 bits), $4000-$5FFF ram bank / RTC select.
     * TODO: RAM enable ($0000-$1FFF), RTC latch ($6000-$7FFF), RTC registers. */
    if (addr >= 0x2000 && addr < 0x4000) {
        cpu.rom_bank = value ? (value & 0x7F) : 1;
    } else if (addr >= 0x4000 && addr < 0x6000) {
        cpu.ram_bank = value & 0x03;
    }
}
