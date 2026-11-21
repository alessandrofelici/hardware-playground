/* rootclock simulator -- display/beeper.c's API without the I2S audio: each
 * beep is a line on stderr, so it never mixes with console replies. */
#include <stdio.h>
#include "beeper.h"

bool beeper_init(void) { return true; }
void beeper_beep(void) { fprintf(stderr, "sim: beep\n"); }
void beeper_process(void) {}
