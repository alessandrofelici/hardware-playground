/* rootclock -- TurnLimitAlarm. See rc_alarm.h. */
#include "rc_alarm.h"

void rc_alarm_reset(rc_alarm_t *a) {
    a->fired_this_turn = false;
}

void rc_alarm_sync(rc_alarm_t *a, uint32_t turn_ms, uint32_t limit_ms) {
    a->fired_this_turn = rc_alarm_over(turn_ms, limit_ms);
}

bool rc_alarm_check(rc_alarm_t *a, uint32_t turn_ms, uint32_t limit_ms,
                    bool running) {
    if (!running || a->fired_this_turn || !rc_alarm_over(turn_ms, limit_ms)) {
        return false;
    }
    a->fired_this_turn = true;
    return true;
}

bool rc_alarm_over(uint32_t turn_ms, uint32_t limit_ms) {
    return limit_ms != 0u && turn_ms >= limit_ms;
}
