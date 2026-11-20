/* rootclock -- GameClock. See rc_game.h for the transition table.
 *
 * Every clock value is derived from the players' stopwatches, and only this
 * file starts or stops them, so "exactly one runs in RUNNING, none otherwise"
 * and "global == sum of totals" hold by construction. */
#include "rc_game.h"

#include <string.h>

static void rc_game_reset_players(rc_game_t *g) {
    for (uint8_t i = 0u; i < RC_SEATS; i++) {
        rc_player_init(&g->players[i], i);
    }
    g->active_seat = RC_NO_SEAT;
    g->viewed_seat = RC_NO_SEAT;
    rc_alarm_reset(&g->alarm);
}

static bool rc_game_has_active(const rc_game_t *g) {
    return g->active_seat < RC_SEATS;
}

static void rc_game_open_settings(rc_game_t *g) {
    g->return_state = g->state;
    rc_menu_reset(&g->menu);
    g->state = RC_STATE_SETTINGS;
}

void rc_game_init(rc_game_t *g) {
    memset(g, 0, sizeof *g);
    g->state = RC_STATE_READY;
    g->return_state = RC_STATE_READY;
    rc_game_reset_players(g);
    rc_settings_defaults(&g->settings);
    rc_menu_reset(&g->menu);
    g->game_seq = 0u;
}

static rc_events_t rc_apply_ready(rc_game_t *g, rc_action_t a, uint32_t now_ms) {
    switch (a.kind) {
    case RC_ACT_START:
        g->active_seat = 0u;
        rc_player_begin_turn(&g->players[0], now_ms);
        rc_alarm_reset(&g->alarm);
        g->state = RC_STATE_RUNNING;
        break;
    case RC_ACT_OPEN_SETTINGS:
        rc_game_open_settings(g);
        break;
    default:
        break;
    }
    return RC_EVT_NONE;
}

static rc_events_t rc_apply_running(rc_game_t *g, rc_action_t a, uint32_t now_ms) {
    if (!rc_game_has_active(g)) {
        return RC_EVT_NONE;   /* unreachable by construction */
    }
    switch (a.kind) {
    case RC_ACT_SWITCH:
        if (a.seat >= RC_SEATS || a.seat == g->active_seat) {
            break;   /* invalid seat, or own button: changes nothing (G6) */
        }
        rc_player_end_turn(&g->players[g->active_seat], now_ms);
        rc_player_begin_turn(&g->players[a.seat], now_ms);
        g->active_seat = a.seat;
        rc_alarm_reset(&g->alarm);
        break;
    case RC_ACT_PAUSE:
        rc_player_pause(&g->players[g->active_seat], now_ms);
        g->state = RC_STATE_PAUSED;
        break;
    default:
        break;
    }
    return RC_EVT_NONE;
}

static rc_events_t rc_apply_paused(rc_game_t *g, rc_action_t a, uint32_t now_ms) {
    if (!rc_game_has_active(g)) {
        return RC_EVT_NONE;   /* unreachable by construction */
    }
    switch (a.kind) {
    case RC_ACT_RESUME:
        rc_player_resume(&g->players[g->active_seat], now_ms);
        g->state = RC_STATE_RUNNING;
        break;
    case RC_ACT_OPEN_SETTINGS:
        rc_game_open_settings(g);
        break;
    case RC_ACT_END_GAME:
        rc_player_end_turn(&g->players[g->active_seat], now_ms);
        g->viewed_seat = g->active_seat;
        g->state = RC_STATE_OVER;
        return RC_EVT_GAME_OVER;
    default:
        break;
    }
    return RC_EVT_NONE;
}

static rc_events_t rc_apply_settings(rc_game_t *g, rc_action_t a, uint32_t now_ms) {
    switch (a.kind) {
    case RC_ACT_MENU_UP:
        rc_menu_move(&g->menu, -1);
        break;
    case RC_ACT_MENU_DOWN:
        rc_menu_move(&g->menu, +1);
        break;
    case RC_ACT_MENU_PREV:
        rc_menu_change(&g->menu, &g->settings, -1);
        break;
    case RC_ACT_MENU_NEXT:
        rc_menu_change(&g->menu, &g->settings, +1);
        break;
    case RC_ACT_MENU_DONE:
        g->state = g->return_state;
        /* The limit may have changed under a paused turn: a turn already
         * past the new limit goes red silently (G13). */
        if (g->return_state == RC_STATE_PAUSED && rc_game_has_active(g) &&
            g->players[g->active_seat].in_turn) {
            rc_alarm_sync(&g->alarm,
                          rc_player_turn_ms(&g->players[g->active_seat], now_ms),
                          rc_settings_limit_ms(&g->settings));
        }
        break;
    default:
        break;
    }
    return RC_EVT_NONE;
}

static rc_events_t rc_apply_over(rc_game_t *g, rc_action_t a) {
    switch (a.kind) {
    case RC_ACT_VIEW:
        if (a.seat < RC_SEATS) {
            g->viewed_seat = a.seat;
        }
        break;
    case RC_ACT_NEW_GAME:
        rc_game_reset_players(g);   /* settings kept */
        g->game_seq++;
        g->state = RC_STATE_READY;
        break;
    default:
        break;
    }
    return RC_EVT_NONE;
}

rc_events_t rc_game_apply(rc_game_t *g, rc_action_t a, uint32_t now_ms) {
    switch (g->state) {
    case RC_STATE_READY:    return rc_apply_ready(g, a, now_ms);
    case RC_STATE_RUNNING:  return rc_apply_running(g, a, now_ms);
    case RC_STATE_PAUSED:   return rc_apply_paused(g, a, now_ms);
    case RC_STATE_SETTINGS: return rc_apply_settings(g, a, now_ms);
    case RC_STATE_OVER:     return rc_apply_over(g, a);
    default:                return RC_EVT_NONE;
    }
}

rc_events_t rc_game_tick(rc_game_t *g, uint32_t now_ms) {
    if (g->state != RC_STATE_RUNNING || !rc_game_has_active(g)) {
        return RC_EVT_NONE;
    }
    uint32_t turn = rc_player_turn_ms(&g->players[g->active_seat], now_ms);
    return rc_alarm_check(&g->alarm, turn, rc_settings_limit_ms(&g->settings),
                          true)
               ? RC_EVT_BEEP
               : RC_EVT_NONE;
}

uint32_t rc_game_turn_ms(const rc_game_t *g, uint8_t seat, uint32_t now_ms) {
    return (seat < RC_SEATS) ? rc_player_turn_ms(&g->players[seat], now_ms) : 0u;
}

uint32_t rc_game_total_ms(const rc_game_t *g, uint8_t seat, uint32_t now_ms) {
    return (seat < RC_SEATS) ? rc_player_total_ms(&g->players[seat], now_ms) : 0u;
}

uint32_t rc_game_global_ms(const rc_game_t *g, uint32_t now_ms) {
    uint32_t sum = 0u;
    for (uint8_t i = 0u; i < RC_SEATS; i++) {
        sum += rc_player_total_ms(&g->players[i], now_ms);
    }
    return sum;
}

uint8_t rc_game_shown_seat(const rc_game_t *g) {
    switch (g->state) {
    case RC_STATE_RUNNING:
    case RC_STATE_PAUSED:
        return g->active_seat;
    case RC_STATE_OVER:
        return g->viewed_seat;
    case RC_STATE_SETTINGS:
        return (g->return_state == RC_STATE_READY) ? 0u : g->active_seat;
    case RC_STATE_READY:
    default:
        return 0u;
    }
}

bool rc_game_turn_over_limit(const rc_game_t *g, uint32_t now_ms) {
    if (g->state == RC_STATE_READY || g->state == RC_STATE_OVER ||
        !rc_game_has_active(g)) {
        return false;
    }
    const rc_player_t *p = &g->players[g->active_seat];
    return p->in_turn &&
           rc_alarm_over(rc_player_turn_ms(p, now_ms),
                         rc_settings_limit_ms(&g->settings));
}
