/* ppu.c — CGB picture processing unit: tilemaps + window + sprites -> RGBA.
 * STATUS: skeleton. Produces the framebuffer the frontend blits; the scanline
 * renderer and STAT/LY timing are TODO (ARCHITECTURE.md §4).
 *
 * CGB specifics that must be first-class: VRAM bank attributes, BG/OBJ palette RAM
 * ($FF68-$FF6B), priority, 8x16 sprites, double-speed timing.
 */
#include "gb.h"
#include "hal_internal.h"
#include <string.h>

extern uint8_t vram[];
extern uint8_t oam[];
extern uint8_t io[];

static uint8_t framebuffer[GB_SCREEN_W * GB_SCREEN_H * 4];

const uint8_t *gb_framebuffer(void) { return framebuffer; }

/* PPU mode timing per scanline (T-cycles): OAM 80, draw ~172, hblank ~204;
 * 144 visible lines + 10 vblank lines = 154 lines * 456 = 70224 T-cycles/frame. */
static uint32_t dot = 0;
static int ly = 0;

void ppu_step(uint32_t tcycles) {
    dot += tcycles;
    while (dot >= 456) {
        dot -= 456;
        /* TODO: render scanline `ly` (BG, window, sprites) into framebuffer. */
        ly = (ly + 1) % 154;
        io[0x44] = (uint8_t)ly;                /* LY */
        if (ly == 144) int_request(0x01);      /* VBlank IRQ */
    }
}
