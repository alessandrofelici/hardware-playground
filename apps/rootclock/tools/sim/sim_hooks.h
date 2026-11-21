/* rootclock simulator -- extension points for simulated peripherals beyond
 * the LCD, buttons and console. sim_hooks.c implements them. */
#ifndef ROOTCLOCK_SIM_HOOKS_H
#define ROOTCLOCK_SIM_HOOKS_H
#include <stdint.h>
void sim_hooks_init(void);            /* once, from board_init()      */
void sim_hooks_poll(uint32_t now_ms); /* every fwog_power_poll()      */
#endif
