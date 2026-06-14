/* timer_irq_input.c — DIV/TIMA timers, interrupt controller, joypad.
 * Grouped: small, tightly-coupled devices. STATUS: skeleton.
 */
#include "gb.h"
#include "hal_internal.h"

extern uint8_t io[];
extern uint8_t ie_reg;

/* --- timers ($FF04 DIV, $FF05 TIMA, $FF06 TMA, $FF07 TAC) --------------------- */
static uint32_t div_acc, tima_acc;
void timer_step(uint32_t tcycles) {
    div_acc += tcycles;
    while (div_acc >= 256) { div_acc -= 256; io[0x04]++; }   /* DIV @ 16384 Hz */
    if (io[0x07] & 0x04) {                                    /* TAC enable */
        static const uint16_t period[4] = {1024, 16, 64, 256};
        tima_acc += tcycles;
        uint16_t p = period[io[0x07] & 3];
        while (tima_acc >= p) {
            tima_acc -= p;
            if (++io[0x05] == 0) { io[0x05] = io[0x06]; int_request(0x04); }  /* overflow */
        }
    }
}

/* --- interrupts ($FF0F IF, $FFFF IE) ----------------------------------------- */
void int_request(uint8_t mask) { io[0x0F] |= mask; }
int  int_pending(void) { return ie_reg & io[0x0F] & 0x1F; }

void int_service(void) {
    if (!cpu.ime) return;
    int p = int_pending();
    if (!p) return;
    static const uint16_t vec[5] = {0x40, 0x48, 0x50, 0x58, 0x60};
    for (int i = 0; i < 5; i++) {
        if (p & (1 << i)) {
            cpu.ime = 0; cpu.halted = 0;
            io[0x0F] &= ~(1 << i);
            push16(cpu.pc);
            cpu.pc = vec[i];
            return;
        }
    }
}

/* --- joypad ($FF00) ----------------------------------------------------------- */
static uint8_t buttons;   /* bit0 A b1 B b2 Sel b3 Start b4 R b5 L b6 U b7 D */
void gb_set_buttons(uint8_t mask) { buttons = mask; }

uint8_t joypad_read(void) {
    uint8_t sel = io[0x00] & 0x30;
    uint8_t lo = 0x0F;
    if (!(sel & 0x10)) lo &= ~(buttons & 0x0F);          /* direction... mapped below */
    if (!(sel & 0x20)) lo &= ~((buttons >> 4) & 0x0F);   /* TODO: correct bit mapping */
    return (sel | lo | 0xC0);
}
