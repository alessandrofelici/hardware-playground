/* rootclock -- GameClock: the aggregate root and state machine.
 *
 * Transitions (documents/03 state diagram):
 *   READY    --START-->          RUNNING  (seat 0 begins its turn)
 *   READY    --OPEN_SETTINGS-->  SETTINGS (return_state = READY, menu row 0)
 *   RUNNING  --NEXT-->           RUNNING  (active ends its turn, the next seat
 *                                          in the rotation begins one; see
 *                                          rc_game_next_seat)
 *   RUNNING  --SWITCH s-->       RUNNING  (active ends its turn, s begins one;
 *                                          s == active is a no-op. API only)
 *   RUNNING  --PAUSE-->          PAUSED   (active turn stops, not ended;
 *                                          viewed_seat = active_seat)
 *   PAUSED   --VIEW_NEXT-->      PAUSED   (viewed_seat = next seat: view only,
 *                                          no clock changes)
 *   PAUSED   --RESUME-->         RUNNING  (the PAUSED turn continues, whatever
 *                                          card was being viewed;
 *                                          viewed_seat = active_seat)
 *   PAUSED   --OPEN_SETTINGS-->  SETTINGS (return_state = PAUSED, menu row 0)
 *   SETTINGS --MENU_UP/DOWN-->   SETTINGS (cursor)
 *   SETTINGS --MENU_PREV/NEXT--> SETTINGS (value -/+ on the cursor row)
 *   SETTINGS --MENU_DONE-->      return_state (alarm re-synced, see rc_alarm.h)
 *   PAUSED   --END_GAME-->       OVER     (active turn ended and logged;
 *                                          viewed_seat = last active;
 *                                          returns RC_EVT_GAME_OVER)
 *   OVER     --VIEW_NEXT-->      OVER     (viewed_seat = next seat)
 *   OVER     --VIEW s-->         OVER     (viewed_seat = s. API only)
 *   OVER     --SEND-->           OVER     (nothing changes; returns
 *                                          RC_EVT_SEND for the uplink)
 *   OVER     --NEW_GAME-->       READY    (clocks and logs zeroed, settings
 *                                          kept, game_seq++)
 * Any action not listed for the current state is ignored (returns NONE and
 * changes nothing).
 *
 * Invariants (documents/03): global == sum of player totals at every instant;
 * exactly one turn stopwatch runs in RUNNING and none in any other state;
 * nickname ids stay a permutation; the alarm fires at most once per turn. */
#ifndef RC_GAME_H
#define RC_GAME_H
#include "rc_alarm.h"
#include "rc_player.h"
#include "rc_settings.h"
#include "rc_types.h"

typedef struct {
    rc_state_t    state;
    rc_state_t    return_state;  /* READY or PAUSED, for MENU_DONE         */
    uint8_t       active_seat;   /* current/last turn's seat; RC_NO_SEAT
                                    before START                           */
    uint8_t       viewed_seat;   /* OVER: the card shown; else unused      */
    rc_player_t   players[RC_SEATS];
    rc_settings_t settings;
    rc_menu_t     menu;
    rc_alarm_t    alarm;         /* for the active seat's current turn     */
    uint32_t      game_seq;      /* bumped by NEW_GAME                     */
} rc_game_t;

/* READY, default settings, zero clocks, game_seq 0. */
void rc_game_init(rc_game_t *g);

/* Apply one action (from rc_input_map) at now_ms. Returns events. */
rc_events_t rc_game_apply(rc_game_t *g, rc_action_t a, uint32_t now_ms);

/* Advance time-driven behaviour: while RUNNING, check the alarm for the
 * active turn against the current limit and return RC_EVT_BEEP on the
 * crossing. Call every loop iteration. */
rc_events_t rc_game_tick(rc_game_t *g, uint32_t now_ms);

/* Clock queries. `seat` 0..3; an out-of-range seat returns 0. */
uint32_t rc_game_turn_ms(const rc_game_t *g, uint8_t seat, uint32_t now_ms);
uint32_t rc_game_total_ms(const rc_game_t *g, uint8_t seat, uint32_t now_ms);

/* Sum of all players' totals (so pause stops it and the invariant holds by
 * construction). */
uint32_t rc_game_global_ms(const rc_game_t *g, uint32_t now_ms);

/* The seat after `seat` in the rotation of rc_settings_players() seats:
 * seat + 1, wrapping to 0 after the last player. A seat outside the rotation
 * (e.g. P4 after the count dropped to 3, or RC_NO_SEAT) also goes to 0. */
uint8_t rc_game_next_seat(const rc_game_t *g, uint8_t seat);

/* The seat S1 shows: READY -> 0; RUNNING -> active_seat; PAUSED -> viewed_seat;
 * OVER -> viewed_seat; SETTINGS -> the seat S1 would show in return_state. */
uint8_t rc_game_shown_seat(const rc_game_t *g);

/* Average completed-turn time for `seat` (rc_player_avg_turn_ms). False when
 * the seat has no completed turn or is out of range. */
bool rc_game_avg_turn_ms(const rc_game_t *g, uint8_t seat, uint32_t *avg_ms);

/* Red digits: the active seat is in a turn and that turn is at or past a
 * non-zero limit (rc_alarm_over). False in READY and OVER. */
bool rc_game_turn_over_limit(const rc_game_t *g, uint32_t now_ms);

#endif
