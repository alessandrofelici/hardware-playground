/* rootclock -- Stopwatch. See rc_stopwatch.h.
 *
 * Every interval is measured as now_ms - started_ms in uint32 arithmetic, so
 * a run interval that straddles the 2^32 ms wrap still comes out right. */
#include "rc_stopwatch.h"

void rc_stopwatch_reset(rc_stopwatch_t *sw) {
    sw->running = false;
    sw->started_ms = 0u;
    sw->accumulated_ms = 0u;
}

void rc_stopwatch_start(rc_stopwatch_t *sw, uint32_t now_ms) {
    if (sw->running) {
        return;
    }
    sw->running = true;
    sw->started_ms = now_ms;
}

void rc_stopwatch_stop(rc_stopwatch_t *sw, uint32_t now_ms) {
    if (!sw->running) {
        return;
    }
    sw->accumulated_ms += now_ms - sw->started_ms;
    sw->running = false;
}

uint32_t rc_stopwatch_elapsed_ms(const rc_stopwatch_t *sw, uint32_t now_ms) {
    uint32_t ms = sw->accumulated_ms;
    if (sw->running) {
        ms += now_ms - sw->started_ms;
    }
    return ms;
}
