/* rootclock simulator: pico/rand.h. SIM_SEED makes game ids repeatable. */
#ifndef ROOTCLOCK_SIM_PICO_RAND_H
#define ROOTCLOCK_SIM_PICO_RAND_H
#include <stdint.h>
uint64_t get_rand_64(void);
#endif
