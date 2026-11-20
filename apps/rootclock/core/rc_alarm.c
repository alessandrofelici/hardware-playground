/* rootclock -- TurnLimitAlarm. STUB: implemented on feat/rootclock-core. */
#include "rc_alarm.h"

void rc_alarm_reset(rc_alarm_t *a) { (void)a; }
void rc_alarm_sync(rc_alarm_t *a, uint32_t turn_ms, uint32_t limit_ms) { (void)a; (void)turn_ms; (void)limit_ms; }
bool rc_alarm_check(rc_alarm_t *a, uint32_t turn_ms, uint32_t limit_ms, bool running) {
    (void)a; (void)turn_ms; (void)limit_ms; (void)running; return false;
}
bool rc_alarm_over(uint32_t turn_ms, uint32_t limit_ms) { (void)turn_ms; (void)limit_ms; return false; }
