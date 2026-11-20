/* rootclock -- Player and TurnLog. See rc_player.h.
 *
 * A turn's duration is its stopwatch's elapsed time, which already excludes
 * paused intervals. Ending a turn moves that duration into the log, so
 * total == log sum + current turn holds before and after every call. */
#include "rc_player.h"

#include <string.h>

/* ---- TurnLog ---- */

void rc_turnlog_clear(rc_turnlog_t *log) {
    memset(log, 0, sizeof *log);
}

void rc_turnlog_append(rc_turnlog_t *log, uint32_t ms) {
    if (log->count < RC_MAX_TURNS) {
        log->turns_ms[log->count] = ms;
        log->count++;
    } else {
        /* Full: fold into the last slot so the sum is preserved. */
        log->turns_ms[RC_MAX_TURNS - 1u] += ms;
        log->truncated = true;
    }
}

uint32_t rc_turnlog_sum_ms(const rc_turnlog_t *log) {
    uint32_t sum = 0u;
    for (uint16_t i = 0u; i < log->count; i++) {
        sum += log->turns_ms[i];
    }
    return sum;
}

/* ---- Player ---- */

void rc_player_init(rc_player_t *p, uint8_t seat) {
    memset(p, 0, sizeof *p);   /* not in turn, stopwatch reset, log empty */
    p->seat = seat;
}

void rc_player_begin_turn(rc_player_t *p, uint32_t now_ms) {
    if (p->in_turn) {
        rc_player_end_turn(p, now_ms);
    }
    rc_stopwatch_reset(&p->turn);
    rc_stopwatch_start(&p->turn, now_ms);
    p->in_turn = true;
    if (p->turns_started < UINT16_MAX) {
        p->turns_started++;
    }
}

void rc_player_end_turn(rc_player_t *p, uint32_t now_ms) {
    if (!p->in_turn) {
        return;
    }
    rc_stopwatch_stop(&p->turn, now_ms);
    rc_turnlog_append(&p->log, rc_stopwatch_elapsed_ms(&p->turn, now_ms));
    rc_stopwatch_reset(&p->turn);
    p->in_turn = false;
}

void rc_player_pause(rc_player_t *p, uint32_t now_ms) {
    if (p->in_turn) {
        rc_stopwatch_stop(&p->turn, now_ms);
    }
}

void rc_player_resume(rc_player_t *p, uint32_t now_ms) {
    if (p->in_turn) {
        rc_stopwatch_start(&p->turn, now_ms);
    }
}

uint32_t rc_player_turn_ms(const rc_player_t *p, uint32_t now_ms) {
    return p->in_turn ? rc_stopwatch_elapsed_ms(&p->turn, now_ms) : 0u;
}

uint32_t rc_player_total_ms(const rc_player_t *p, uint32_t now_ms) {
    return rc_turnlog_sum_ms(&p->log) + rc_player_turn_ms(p, now_ms);
}
