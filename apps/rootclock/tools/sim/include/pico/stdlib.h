/* rootclock simulator: the time and stdio calls rootclock takes from the
 * SDK's pico/stdlib.h. Time is CLOCK_MONOTONIC from the simulator's start. */
#ifndef ROOTCLOCK_SIM_PICO_STDLIB_H
#define ROOTCLOCK_SIM_PICO_STDLIB_H
#include <stdbool.h>
#include <stdint.h>

#define PICO_ERROR_TIMEOUT (-1)

typedef uint64_t absolute_time_t;   /* microseconds since start */

absolute_time_t get_absolute_time(void);
uint32_t to_ms_since_boot(absolute_time_t t);
absolute_time_t make_timeout_time_ms(uint32_t ms);
absolute_time_t make_timeout_time_us(uint64_t us);
bool time_reached(absolute_time_t t);
void sleep_ms(uint32_t ms);
int getchar_timeout_us(uint32_t timeout_us);
#endif
