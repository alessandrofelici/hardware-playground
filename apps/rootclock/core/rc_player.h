/* rootclock -- Player (timing data for one seat) and TurnLog.
 *
 * Who a seat IS (nickname, faction) lives in rc_settings_t, not here: this is
 * only the seat's clocks. Invariant (documents/03): at every instant
 *     rc_player_total_ms(p, now) == rc_turnlog_sum_ms(&p->log)
 *                                   + rc_player_turn_ms(p, now)
 * including after the log overflows (overflow folds into the last slot). */
#ifndef RC_PLAYER_H
#define RC_PLAYER_H
#include "rc_stopwatch.h"
#include "rc_types.h"

/* ---- TurnLog: durations of COMPLETED turns, oldest first ---- */
typedef struct {
    uint32_t turns_ms[RC_MAX_TURNS];
    uint16_t count;       /* entries used, <= RC_MAX_TURNS                 */
    bool     truncated;   /* more turns than slots: extra time folded into
                             turns_ms[RC_MAX_TURNS - 1]                     */
} rc_turnlog_t;

void rc_turnlog_clear(rc_turnlog_t *log);

/* Append one completed turn. When count == RC_MAX_TURNS, adds `ms` to the
 * last slot and sets `truncated` instead. */
void rc_turnlog_append(rc_turnlog_t *log, uint32_t ms);

/* Sum of all entries. */
uint32_t rc_turnlog_sum_ms(const rc_turnlog_t *log);

/* ---- Player ---- */
typedef struct {
    uint8_t        seat;           /* 0..3                                    */
    bool           in_turn;        /* a turn has begun and not yet ended      */
    rc_stopwatch_t turn;           /* the current turn; stopped while paused  */
    uint16_t       turns_started;  /* turns begun this game (shown "TURN n")  */
    rc_turnlog_t   log;            /* completed turns                         */
} rc_player_t;

/* Zero clocks, empty log, not in turn. */
void rc_player_init(rc_player_t *p, uint8_t seat);

/* Start a new turn at 0, running. turns_started++. If already in a turn,
 * that turn is ended first (rc_player_end_turn). */
void rc_player_begin_turn(rc_player_t *p, uint32_t now_ms);

/* End the current turn: stop it, append its elapsed time (paused time
 * excluded) to the log, leave in_turn false. No-op if not in a turn. */
void rc_player_end_turn(rc_player_t *p, uint32_t now_ms);

/* Stop / restart the current turn without ending it. No-op if not in a turn. */
void rc_player_pause(rc_player_t *p, uint32_t now_ms);
void rc_player_resume(rc_player_t *p, uint32_t now_ms);

/* Elapsed time of the current turn; 0 when not in a turn. */
uint32_t rc_player_turn_ms(const rc_player_t *p, uint32_t now_ms);

/* Completed turns plus the current turn. */
uint32_t rc_player_total_ms(const rc_player_t *p, uint32_t now_ms);

#endif
