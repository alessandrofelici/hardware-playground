/* rootclock simulator -- the five buttons through the BSP's real debouncer.
 *
 * Built with HOST_TEST against the BSP's input/buttons.h, so it sees the
 * real fwog_btn_step() and nothing else; sim_bsp.c, which includes the
 * simulator's own fwog_display.h, calls it through sim_buttons_step(). */
#include "input/buttons.h"
#include "sim_buttons.h"

static fwog_btn_state_t s_btn[FWOG_BTN_COUNT];

void sim_buttons_reset(void) {
    for (unsigned i = 0u; i < FWOG_BTN_COUNT; i++) {
        s_btn[i] = (fwog_btn_state_t){0};
    }
}

sim_buttons_t sim_buttons_step(uint8_t raw, uint32_t now_ms) {
    sim_buttons_t out = {0};
    for (unsigned i = 0u; i < FWOG_BTN_COUNT; i++) {
        const fwog_btn_event_t e =
            fwog_btn_step(&s_btn[i], (raw >> i) & 1u, now_ms);
        if (e.down) out.down |= (uint8_t)(1u << i);
        if (e.pressed) out.pressed |= (uint8_t)(1u << i);
        if (e.released) out.released |= (uint8_t)(1u << i);
    }
    out.red_held_ms = fwog_btn_held_ms(&s_btn[FWOG_BTN_RED], now_ms);
    return out;
}
