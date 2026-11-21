/* rootclock -- TurnLimitAlarm: the per-turn time limit.
 *
 * Semantics (documents/01 section 4, rule 6, and G13):
 *   - a running turn crossing the limit beeps ONCE and the digits turn red;
 *   - the digits stay red and keep counting for the rest of that turn;
 *   - the next turn starts white again;
 *   - a limit change that leaves the current turn already past the limit
 *     makes it red WITHOUT a beep. */
#ifndef RC_ALARM_H
#define RC_ALARM_H
#include "rc_types.h"

typedef struct {
    bool fired_this_turn;
} rc_alarm_t;

/* Not fired. Call when a new turn begins. */
void rc_alarm_reset(rc_alarm_t *a);

/* Re-derive the fired flag after the limit may have changed:
 * fired_this_turn = rc_alarm_over(turn_ms, limit_ms). Call on leaving
 * settings, so a turn already past a new limit goes red silently. */
void rc_alarm_sync(rc_alarm_t *a, uint32_t turn_ms, uint32_t limit_ms);

/* True exactly once per turn: on the first call with running == true,
 * limit_ms != 0, turn_ms >= limit_ms and the alarm not yet fired. Marks it
 * fired. False otherwise. */
bool rc_alarm_check(rc_alarm_t *a, uint32_t turn_ms, uint32_t limit_ms,
                    bool running);

/* Whether the digits should be red: limit_ms != 0 && turn_ms >= limit_ms. */
bool rc_alarm_over(uint32_t turn_ms, uint32_t limit_ms);

#endif
