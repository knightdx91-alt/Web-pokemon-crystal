/* apu.c — 4-channel sound -> interleaved stereo PCM ring for WebAudio.
 * STATUS: skeleton. Channel synthesis (2 pulse, wave, noise), frame sequencer,
 * and mixing are TODO (ARCHITECTURE.md §4).
 */
#include "gb.h"
#include "hal_internal.h"

#define RING 8192
static int16_t ring[RING * 2];
static uint32_t head, tail;   /* in sample frames */

void apu_step(uint32_t tcycles) {
    (void)tcycles;
    /* TODO: advance channels, push mixed frames into ring at 48 kHz. */
}

const int16_t *gb_audio_samples(void) { return ring; }
uint32_t gb_audio_available(void) { return (head - tail) & (RING - 1); }
void gb_audio_consume(uint32_t frames) { tail = (tail + frames) & (RING - 1); }
