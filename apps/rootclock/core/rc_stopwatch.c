/* rootclock -- Stopwatch. STUB: implemented on feat/rootclock-core. */
#include "rc_stopwatch.h"

void rc_stopwatch_reset(rc_stopwatch_t *sw) { (void)sw; }
void rc_stopwatch_start(rc_stopwatch_t *sw, uint32_t now_ms) { (void)sw; (void)now_ms; }
void rc_stopwatch_stop(rc_stopwatch_t *sw, uint32_t now_ms) { (void)sw; (void)now_ms; }
uint32_t rc_stopwatch_elapsed_ms(const rc_stopwatch_t *sw, uint32_t now_ms) {
    (void)sw; (void)now_ms; return 0u;
}
