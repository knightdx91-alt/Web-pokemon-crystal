/* ppu.c — Game Boy Color picture processing unit.
 *
 * Scanline renderer: background + window + sprites -> 160x144 RGBA framebuffer,
 * with LCDC/STAT/LY/LYC timing. CGB-first (Crystal is a Color game): VRAM bank-1
 * tile attributes, 8 BG + 8 OBJ palettes in palette RAM, per-tile priority/flip.
 * A DMG fallback (BGP/OBP shades) is kept for the boot handoff.
 *
 * Owns palette RAM ($FF68-$FF6B); bus.c forwards those registers here.
 */
#include "gb.h"
#include "hal_internal.h"

extern uint8_t vram[];   /* 2 banks: [bank*0x2000 + (addr-0x8000)] */
extern uint8_t oam[];    /* $FE00-$FE9F, 40 sprites x 4 bytes */
extern uint8_t io[];     /* $FF00-$FF7F */

/* I/O register indices (offset from $FF00) */
enum { R_LCDC=0x40, R_STAT=0x41, R_SCY=0x42, R_SCX=0x43, R_LY=0x44, R_LYC=0x45,
       R_BGP=0x47, R_OBP0=0x48, R_OBP1=0x49, R_WY=0x4A, R_WX=0x4B };

static uint8_t framebuffer[GB_SCREEN_W * GB_SCREEN_H * 4];
static uint8_t bg_palram[64];   /* 8 palettes x 4 colors x 2 bytes (RGB555 LE) */
static uint8_t obj_palram[64];
static uint8_t bcps, ocps;      /* palette index/auto-increment regs ($FF68/$FF6A) */
int ppu_cgb = 1;                /* set 0 for DMG behaviour */

/* per-scanline scratch: BG color id (0-3) and BG-over-OBJ priority flag */
static uint8_t line_bg_color[GB_SCREEN_W];
static uint8_t line_bg_prio[GB_SCREEN_W];
static int window_line;         /* internal window line counter */

const uint8_t *gb_framebuffer(void) { return framebuffer; }

/* ---- palette RAM access (forwarded from bus.c) ------------------------------ */
uint32_t g_palw_bg, g_palw_obj;            /* debug: palette RAM write counts */
uint32_t gb_dbg_palw_bg(void)  { return g_palw_bg; }
uint32_t gb_dbg_palw_obj(void) { return g_palw_obj; }
void ppu_pal_write(uint16_t reg, uint8_t v) {
    if (reg == 0x69) g_palw_bg++;
    if (reg == 0x6B) g_palw_obj++;
    switch (reg) {
        case 0x68: bcps = v; break;
        case 0x69: bg_palram[bcps & 0x3F] = v;  if (bcps & 0x80) bcps = 0x80 | ((bcps + 1) & 0x3F); break;
        case 0x6A: ocps = v; break;
        case 0x6B: obj_palram[ocps & 0x3F] = v; if (ocps & 0x80) ocps = 0x80 | ((ocps + 1) & 0x3F); break;
    }
}
uint8_t ppu_pal_read(uint16_t reg) {
    switch (reg) {
        case 0x68: return bcps;
        case 0x69: return bg_palram[bcps & 0x3F];
        case 0x6A: return ocps;
        case 0x6B: return obj_palram[ocps & 0x3F];
    }
    return 0xFF;
}

/* ---- colour conversion ------------------------------------------------------ */
static inline void put_rgb555(int x, int y, const uint8_t *pal, int idx) {
    int lo = pal[idx * 2], hi = pal[idx * 2 + 1];
    int c = lo | (hi << 8);
    int r = (c & 0x1F), g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;
    uint8_t *p = &framebuffer[(y * GB_SCREEN_W + x) * 4];
    p[0] = (uint8_t)((r << 3) | (r >> 2));
    p[1] = (uint8_t)((g << 3) | (g >> 2));
    p[2] = (uint8_t)((b << 3) | (b >> 2));
    p[3] = 0xFF;
}
/* DMG shades through BGP/OBP: classic 4-level green */
static const uint8_t DMG_SHADE[4][3] = {{224,248,208},{136,192,112},{52,104,86},{8,24,32}};
static inline void put_dmg(int x, int y, uint8_t pal_reg, int colorid) {
    int shade = (pal_reg >> (colorid * 2)) & 3;
    uint8_t *p = &framebuffer[(y * GB_SCREEN_W + x) * 4];
    p[0] = DMG_SHADE[shade][0]; p[1] = DMG_SHADE[shade][1];
    p[2] = DMG_SHADE[shade][2]; p[3] = 0xFF;
}

static inline uint8_t vrd(int bank, uint16_t addr) { return vram[bank * 0x2000 + (addr - 0x8000)]; }

/* 2bpp pixel color id from a tile row */
static inline int tile_colorid(uint16_t tile_addr, int bank, int row, int col, int xflip) {
    uint8_t lo = vrd(bank, tile_addr + row * 2);
    uint8_t hi = vrd(bank, tile_addr + row * 2 + 1);
    int bit = xflip ? col : 7 - col;
    return ((hi >> bit) & 1) << 1 | ((lo >> bit) & 1);
}

/* ---- background + window ---------------------------------------------------- */
static void render_bg(int ly) {
    uint8_t lcdc = io[R_LCDC];
    int signed_td = !(lcdc & 0x10);                 /* tile-data addressing */
    uint16_t bg_map  = (lcdc & 0x08) ? 0x9C00 : 0x9800;
    uint16_t win_map = (lcdc & 0x40) ? 0x9C00 : 0x9800;
    int win_on = (lcdc & 0x20) && (io[R_WY] <= ly);
    int wx = io[R_WX] - 7;
    int scy = io[R_SCY], scx = io[R_SCX];

    for (int x = 0; x < GB_SCREEN_W; x++) {
        int use_win = win_on && x >= wx;
        int mx, my; uint16_t map;
        if (use_win) { map = win_map; mx = x - wx; my = window_line; }
        else         { map = bg_map;  mx = (x + scx) & 0xFF; my = (ly + scy) & 0xFF; }

        int tx = mx >> 3, ty = my >> 3;
        uint16_t ent = map + ty * 32 + tx;
        int tile = vrd(0, ent);
        uint8_t attr = ppu_cgb ? vrd(1, ent) : 0;
        int pal_n   = attr & 0x07;
        int tbank   = (attr >> 3) & 1;
        int xflip   = (attr >> 5) & 1;
        int yflip   = (attr >> 6) & 1;
        int prio    = (attr >> 7) & 1;

        uint16_t tile_addr = signed_td ? (uint16_t)(0x9000 + (int8_t)tile * 16)
                                       : (uint16_t)(0x8000 + tile * 16);
        int row = yflip ? 7 - (my & 7) : (my & 7);
        int col = (mx & 7);
        int cid = tile_colorid(tile_addr, ppu_cgb ? tbank : 0, row, col, xflip);

        line_bg_color[x] = (uint8_t)cid;
        line_bg_prio[x]  = (uint8_t)prio;
        if (ppu_cgb) put_rgb555(x, ly, &bg_palram[pal_n * 8], cid);
        else         put_dmg(x, ly, io[R_BGP], cid);
    }
    if (win_on) window_line++;
}

/* ---- sprites ---------------------------------------------------------------- */
static void render_sprites(int ly) {
    uint8_t lcdc = io[R_LCDC];
    if (!(lcdc & 0x02)) return;
    int h = (lcdc & 0x04) ? 16 : 8;

    /* select up to 10 sprites on this line, keep OAM order (= CGB priority) */
    int idx[10], n = 0;
    for (int i = 0; i < 40 && n < 10; i++) {
        int sy = oam[i * 4] - 16;
        if (ly >= sy && ly < sy + h) idx[n++] = i;
    }
    /* draw lowest priority first so higher-priority (earlier OAM) overwrites */
    for (int k = n - 1; k >= 0; k--) {
        int i = idx[k];
        int sy = oam[i * 4] - 16, sx = oam[i * 4 + 1] - 8;
        int tile = oam[i * 4 + 2];
        uint8_t at = oam[i * 4 + 3];
        int behind = (at >> 7) & 1, yflip = (at >> 6) & 1, xflip = (at >> 5) & 1;
        int dmg_pal = (at >> 4) & 1;
        int cgb_pal = at & 0x07;
        int tbank = ppu_cgb ? ((at >> 3) & 1) : 0;
        if (h == 16) tile &= 0xFE;

        int row = ly - sy;
        if (yflip) row = h - 1 - row;
        uint16_t tile_addr = (uint16_t)(0x8000 + tile * 16);
        for (int c = 0; c < 8; c++) {
            int x = sx + c;
            if (x < 0 || x >= GB_SCREEN_W) continue;
            int cid = tile_colorid(tile_addr, tbank, row, c, xflip);
            if (cid == 0) continue;                       /* transparent */
            /* BG/OBJ priority: BG color 1-3 wins if its priority or sprite "behind" */
            int bg = line_bg_color[x];
            if (bg != 0 && (behind || line_bg_prio[x])) continue;
            if (ppu_cgb) put_rgb555(x, ly, &obj_palram[cgb_pal * 8], cid);
            else         put_dmg(x, ly, io[dmg_pal ? R_OBP1 : R_OBP0], cid);
        }
    }
}

static void render_scanline(int ly) {
    if (!(io[R_LCDC] & 0x80)) {                 /* LCD off: blank line */
        for (int x = 0; x < GB_SCREEN_W; x++) {
            uint8_t *p = &framebuffer[(ly * GB_SCREEN_W + x) * 4];
            p[0] = p[1] = p[2] = 0xFF; p[3] = 0xFF;
        }
        return;
    }
    render_bg(ly);
    render_sprites(ly);
}

/* ---- timing ----------------------------------------------------------------- */
static uint32_t dot = 0;
static int ly = 0;
static int frame_latch = 0;

static int cur_mode = -1;       /* edge-trigger STAT: only fire on a real change */
static int lyc_match = 0;

static void set_mode(int mode) {
    io[R_STAT] = (uint8_t)((io[R_STAT] & ~0x03) | (mode & 3));
    if (mode == cur_mode) return;
    cur_mode = mode;
    /* STAT mode interrupt enables (bits 3-5: HBlank/VBlank/OAM) */
    static const uint8_t bit[3] = {0x08, 0x10, 0x20};
    if (mode < 3 && (io[R_STAT] & bit[mode])) int_request(0x02);
}

static void check_lyc(void) {
    int match = (io[R_LY] == io[R_LYC]);
    if (match) io[R_STAT] |= 0x04; else io[R_STAT] &= ~0x04;
    if (match && !lyc_match && (io[R_STAT] & 0x40)) int_request(0x02);   /* rising edge */
    lyc_match = match;
}

void ppu_step(uint32_t tcycles) {
    if (!(io[R_LCDC] & 0x80)) {                 /* LCD disabled: reset timing */
        dot = 0; ly = 0; io[R_LY] = 0; window_line = 0;
        io[R_STAT] &= ~0x03;
        return;
    }
    dot += tcycles;
    while (dot >= 456) {
        dot -= 456;
        if (ly < 144) render_scanline(ly);      /* render at line end (simple, stable) */
        ly++;
        if (ly == 144) { int_request(0x01); set_mode(1); frame_latch = 1; window_line = 0; }
        else if (ly > 153) { ly = 0; }
        else if (ly < 144) { set_mode(2); }
        io[R_LY] = (uint8_t)ly;
        check_lyc();
    }
    /* within-line mode progression (OAM->draw->hblank) for code that polls STAT */
    if (ly < 144) {
        if (dot < 80)       set_mode(2);
        else if (dot < 252) set_mode(3);
        else                set_mode(0);
    }
}

/* debug accessors: return linear-memory offsets JS can read */
const uint8_t *gb_dbg_bgpal(void)  { return bg_palram; }
const uint8_t *gb_dbg_objpal(void) { return obj_palram; }

int ppu_frame_pending(void) { return frame_latch; }            /* peek, non-consuming */
int ppu_take_frame(void) { int f = frame_latch; frame_latch = 0; return f; }
