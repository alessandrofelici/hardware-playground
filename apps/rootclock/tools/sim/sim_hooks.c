/* rootclock simulator -- nothing beyond the LCD, buttons and console yet. */
#include "sim_hooks.h"
void sim_hooks_init(void) {}
void sim_hooks_poll(uint32_t now_ms) { (void)now_ms; }
