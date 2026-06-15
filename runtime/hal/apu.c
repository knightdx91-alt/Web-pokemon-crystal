/* apu.c — Game Boy sound: 2 pulse + wave + noise -> stereo PCM for WebAudio.
 *
 * Models the four channels, the 512 Hz frame sequencer (length 256 Hz, envelope
 * 64 Hz, sweep 128 Hz), NR50/51/52 mixing, and resamples to a fixed output rate
 * into a ring buffer the frontend drains. Registers $FF10-$FF26 + wave RAM
 * $FF30-$FF3F are forwarded here from bus.c.
 */
#include "gb.h"
#include "hal_internal.h"

#define CPU_HZ      4194304
#define OUT_HZ      32768
#define CYC_PER_SMP (CPU_HZ / OUT_HZ)      /* 128 CPU cycles per output sample */
#define RING        16384                  /* sample frames (stereo) */

static int16_t ring[RING * 2];
static uint32_t head, tail;                /* sample-frame indices */

static uint8_t reg[0x40];                  /* $FF10-$FF3F mirror (offset 0x10..0x3F) */
#define R(a) reg[(a) - 0xFF10]

/* --- per-channel state ---------------------------------------------------- */
static const uint8_t DUTY[4] = {0x01, 0x81, 0x87, 0x7E};  /* 12.5/25/50/75% */

typedef struct { int on, freq, timer, duty_pos, len, vol, env_t, env_dir, env_per, dac; } Pulse;
typedef struct { int on, freq, timer, pos, len, vol, dac; } Wave;
typedef struct { int on, timer, len, vol, env_t, env_dir, env_per, lfsr, width, dac; } Noise;

static Pulse ch1, ch2;
static Wave  ch3;
static Noise ch4;
static int seq_step;            /* frame sequencer step 0..7 */
static uint32_t seq_timer;      /* counts CPU cycles to 8192 (512 Hz) */
static uint32_t smp_acc;        /* cycles toward next output sample */

static void push_sample(int16_t l, int16_t r) {
    if (((head + 1) & (RING - 1)) == (tail & (RING - 1))) return;  /* full */
    ring[(head & (RING - 1)) * 2] = l;
    ring[(head & (RING - 1)) * 2 + 1] = r;
    head++;
}

/* --- frame-sequencer clocks ----------------------------------------------- */
static void clock_length(void) {
    if ((R(0xFF14) & 0x40) && ch1.len > 0 && --ch1.len == 0) ch1.on = 0;
    if ((R(0xFF19) & 0x40) && ch2.len > 0 && --ch2.len == 0) ch2.on = 0;
    if ((R(0xFF1E) & 0x40) && ch3.len > 0 && --ch3.len == 0) ch3.on = 0;
    if ((R(0xFF23) & 0x40) && ch4.len > 0 && --ch4.len == 0) ch4.on = 0;
}
static void clock_env_pulse(Pulse *c) {
    if (c->env_per && --c->env_t <= 0) {
        c->env_t = c->env_per;
        int v = c->vol + (c->env_dir ? 1 : -1);
        if (v >= 0 && v <= 15) c->vol = v;
    }
}
static void clock_env_noise(void) {
    if (ch4.env_per && --ch4.env_t <= 0) {
        ch4.env_t = ch4.env_per;
        int v = ch4.vol + (ch4.env_dir ? 1 : -1);
        if (v >= 0 && v <= 15) ch4.vol = v;
    }
}
static void clock_sweep(void) {
    int sw = R(0xFF10);
    int period = (sw >> 4) & 7, shift = sw & 7;
    if (!period || !shift) return;
    int delta = ch1.freq >> shift;
    int nf = (sw & 0x08) ? ch1.freq - delta : ch1.freq + delta;
    if (nf > 2047) ch1.on = 0; else if (nf >= 0) ch1.freq = nf;
}

static void frame_seq(void) {
    switch (seq_step) {
        case 0: clock_length(); break;
        case 2: clock_length(); clock_sweep(); break;
        case 4: clock_length(); break;
        case 6: clock_length(); clock_sweep(); break;
        case 7: clock_env_pulse(&ch1); clock_env_pulse(&ch2); clock_env_noise(); break;
    }
    seq_step = (seq_step + 1) & 7;
}

/* --- per-call advance ----------------------------------------------------- */
void apu_step(uint32_t tcycles) {
    if (!(R(0xFF26) & 0x80)) {            /* APU off: emit silence, keep buffer fed */
        smp_acc += tcycles;
        while (smp_acc >= CYC_PER_SMP) { smp_acc -= CYC_PER_SMP; push_sample(0, 0); }
        return;
    }
    for (uint32_t i = 0; i < tcycles; i++) {
        /* frequency timers */
        if (--ch1.timer <= 0) { ch1.timer = (2048 - ch1.freq) * 4; ch1.duty_pos = (ch1.duty_pos + 1) & 7; }
        if (--ch2.timer <= 0) { ch2.timer = (2048 - ch2.freq) * 4; ch2.duty_pos = (ch2.duty_pos + 1) & 7; }
        if (--ch3.timer <= 0) { ch3.timer = (2048 - ch3.freq) * 2; ch3.pos = (ch3.pos + 1) & 31; }
        if (--ch4.timer <= 0) {
            static const int div[8] = {8, 16, 32, 48, 64, 80, 96, 112};
            int p = R(0xFF22);
            ch4.timer = div[p & 7] << ((p >> 4) & 0x0F);
            int b = (ch4.lfsr ^ (ch4.lfsr >> 1)) & 1;
            ch4.lfsr = (ch4.lfsr >> 1) | (b << 14);
            if (ch4.width) { ch4.lfsr &= ~(1 << 6); ch4.lfsr |= b << 6; }
        }
        /* frame sequencer @ 512 Hz */
        if (++seq_timer >= 8192) { seq_timer = 0; frame_seq(); }
        /* output sample */
        if (++smp_acc >= CYC_PER_SMP) {
            smp_acc = 0;
            int s1 = (ch1.on && ch1.dac) ? (((DUTY[(R(0xFF11) >> 6) & 3] >> ch1.duty_pos) & 1) ? ch1.vol : 0) : 0;
            int s2 = (ch2.on && ch2.dac) ? (((DUTY[(R(0xFF16) >> 6) & 3] >> ch2.duty_pos) & 1) ? ch2.vol : 0) : 0;
            int wv = 0;
            if (ch3.on && ch3.dac) {
                int sample = reg[0x20 + (ch3.pos >> 1)];   /* wave RAM = reg[0x20..0x2F] */
                sample = (ch3.pos & 1) ? (sample & 0x0F) : (sample >> 4);
                int shift = (R(0xFF1C) >> 5) & 3;
                wv = shift ? (sample >> (shift - 1)) : 0;
            }
            int s4 = (ch4.on && ch4.dac) ? ((~ch4.lfsr & 1) ? ch4.vol : 0) : 0;
            /* panning + master volume (NR51/NR50) */
            int nr51 = R(0xFF25), nr50 = R(0xFF24);
            int l = 0, r = 0;
            if (nr51 & 0x10) l += s1; if (nr51 & 0x01) r += s1;
            if (nr51 & 0x20) l += s2; if (nr51 & 0x02) r += s2;
            if (nr51 & 0x40) l += wv; if (nr51 & 0x04) r += wv;
            if (nr51 & 0x80) l += s4; if (nr51 & 0x08) r += s4;
            int lv = ((nr50 >> 4) & 7) + 1, rv = (nr50 & 7) + 1;
            /* each channel 0..15; 4 channels -> 0..60; scale to int16 with headroom */
            push_sample((int16_t)(l * lv * 28), (int16_t)(r * rv * 28));
        }
    }
}

/* --- register writes (from bus.c) ----------------------------------------- */
static void trigger_pulse(Pulse *c, uint16_t nrx2, uint16_t nrx4) {
    c->on = 1;
    c->vol = R(nrx2) >> 4; c->env_dir = (R(nrx2) >> 3) & 1; c->env_per = R(nrx2) & 7; c->env_t = c->env_per;
    c->dac = (R(nrx2) & 0xF8) != 0;
    if (c->len == 0) c->len = 64;
    c->timer = (2048 - c->freq) * 4;
}

void apu_write(uint16_t addr, uint8_t v) {
    if (addr == 0xFF26) {                 /* NR52 power */
        R(addr) = (R(addr) & 0x0F) | (v & 0x80);
        if (!(v & 0x80)) { for (int a = 0xFF10; a < 0xFF26; a++) R(a) = 0; ch1.on = ch2.on = ch3.on = ch4.on = 0; }
        return;
    }
    if (!(R(0xFF26) & 0x80) && addr < 0xFF26) return;   /* writes ignored while off */
    R(addr) = v;
    switch (addr) {
        case 0xFF11: ch1.len = 64 - (v & 0x3F); break;
        case 0xFF13: ch1.freq = (ch1.freq & 0x700) | v; break;
        case 0xFF14: ch1.freq = (ch1.freq & 0xFF) | ((v & 7) << 8);
                     if (v & 0x80) trigger_pulse(&ch1, 0xFF12, 0xFF14); break;
        case 0xFF16: ch2.len = 64 - (v & 0x3F); break;
        case 0xFF18: ch2.freq = (ch2.freq & 0x700) | v; break;
        case 0xFF19: ch2.freq = (ch2.freq & 0xFF) | ((v & 7) << 8);
                     if (v & 0x80) trigger_pulse(&ch2, 0xFF17, 0xFF19); break;
        case 0xFF1A: ch3.dac = (v & 0x80) != 0; if (!ch3.dac) ch3.on = 0; break;
        case 0xFF1B: ch3.len = 256 - v; break;
        case 0xFF1D: ch3.freq = (ch3.freq & 0x700) | v; break;
        case 0xFF1E: ch3.freq = (ch3.freq & 0xFF) | ((v & 7) << 8);
                     if (v & 0x80) { ch3.on = ch3.dac; ch3.pos = 0; if (ch3.len == 0) ch3.len = 256;
                                     ch3.timer = (2048 - ch3.freq) * 2; } break;
        case 0xFF20: ch4.len = 64 - (v & 0x3F); break;
        case 0xFF21: ch4.vol = v >> 4; ch4.env_dir = (v >> 3) & 1; ch4.env_per = v & 7;
                     ch4.dac = (v & 0xF8) != 0; if (!ch4.dac) ch4.on = 0; break;
        case 0xFF22: ch4.width = (v >> 3) & 1; break;
        case 0xFF23: if (v & 0x80) { ch4.on = ch4.dac; ch4.env_t = ch4.env_per; ch4.lfsr = 0x7FFF;
                                     if (ch4.len == 0) ch4.len = 64; } break;
    }
}
uint8_t apu_read(uint16_t addr) {
    if (addr == 0xFF26)
        return (R(addr) & 0xF0) | (ch1.on) | (ch2.on << 1) | (ch3.on << 2) | (ch4.on << 3) | 0x70;
    return R(addr) | 0x00;
}

/* --- frontend interface --------------------------------------------------- */
const int16_t *gb_audio_samples(void) { return ring; }
uint32_t gb_audio_available(void) { return (head - tail) & (RING - 1); }
void gb_audio_consume(uint32_t frames) { tail += frames; }
uint32_t gb_audio_tail(void) { return tail & (RING - 1); }   /* read offset (frames) */
