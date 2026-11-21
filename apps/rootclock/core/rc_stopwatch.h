/* rootclock -- Stopwatch: accumulates running time across stops.
 *
 * All times are uint32 milliseconds from any monotonic clock. Elapsed time is
 * computed with unsigned subtraction (now_ms - started_ms), which is correct
 * across the uint32 wrap for any interval shorter than 2^31 ms. */
#ifndef RC_STOPWATCH_H
#define RC_STOPWATCH_H
#include "rc_types.h"

typedef struct {
    bool     running;
    uint32_t started_ms;      /* valid while running                    */
    uint32_t accumulated_ms;  /* time from previous run intervals       */
} rc_stopwatch_t;

/* Stopped, zero elapsed. */
void rc_stopwatch_reset(rc_stopwatch_t *sw);

/* Begin a run interval. No-op if already running. */
void rc_stopwatch_start(rc_stopwatch_t *sw, uint32_t now_ms);

/* End the run interval, folding it into accumulated_ms. No-op if stopped. */
void rc_stopwatch_stop(rc_stopwatch_t *sw, uint32_t now_ms);

/* accumulated_ms plus the current interval if running. */
uint32_t rc_stopwatch_elapsed_ms(const rc_stopwatch_t *sw, uint32_t now_ms);

#endif
