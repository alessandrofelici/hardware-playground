/* rootclock simulator -- see sim_buttons.c. */
#ifndef ROOTCLOCK_SIM_BUTTONS_H
#define ROOTCLOCK_SIM_BUTTONS_H
#include <stdint.h>

typedef struct {
    uint8_t  down, pressed, released;   /* bit per button, BSP order */
    uint32_t red_held_ms;
} sim_buttons_t;

void sim_buttons_reset(void);
sim_buttons_t sim_buttons_step(uint8_t raw, uint32_t now_ms);
#endif
