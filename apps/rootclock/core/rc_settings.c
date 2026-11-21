/* rootclock -- Settings and SettingsMenu. See rc_settings.h. */
#include "rc_settings.h"

static const uint32_t rc_limit_options_ms[RC_LIMIT_OPTION_COUNT] = {
    0u,         /* Off   */
    30000u,     /* 0:30  */
    60000u,     /* 1:00  */
    90000u,     /* 1:30  */
    120000u,    /* 2:00  */
    180000u,    /* 3:00  */
    240000u,    /* 4:00  */
    300000u,    /* 5:00  */
    420000u,    /* 7:00  */
    600000u,    /* 10:00 */
    900000u,    /* 15:00 */
};

/* One wrapping step of `cur` over 0..n-1. dir > 0 is +1, dir < 0 is -1,
 * 0 leaves it. An out-of-range `cur` is first brought back into range. */
static uint8_t rc_wrap_step(uint8_t cur, int dir, uint8_t n) {
    if (dir == 0) {
        return cur;
    }
    cur = (uint8_t)(cur % n);
    return (dir > 0) ? (uint8_t)((cur + 1u) % n)
                     : (uint8_t)((cur + n - 1u) % n);
}

void rc_settings_defaults(rc_settings_t *s) {
    static const uint8_t default_faction[RC_SEATS] = {
        RC_FACTION_MARQUISE, RC_FACTION_EYRIE,
        RC_FACTION_WOODLAND, RC_FACTION_VAGABOND,
    };
    s->players = RC_PLAYERS_DEFAULT;
    s->limit_idx = 0u;
    for (uint8_t i = 0u; i < RC_SEATS; i++) {
        s->seat[i].nickname_id = i;            /* Jacob, Alex, Ahmed, Lorenzo */
        s->seat[i].faction_id = default_faction[i];
    }
}

uint32_t rc_limit_option_ms(uint8_t idx) {
    return (idx < RC_LIMIT_OPTION_COUNT) ? rc_limit_options_ms[idx] : 0u;
}

uint32_t rc_settings_limit_ms(const rc_settings_t *s) {
    return rc_limit_option_ms(s->limit_idx);
}

uint8_t rc_settings_players(const rc_settings_t *s) {
    if (s->players < RC_PLAYERS_MIN) return (uint8_t)RC_PLAYERS_MIN;
    if (s->players > RC_PLAYERS_MAX) return (uint8_t)RC_PLAYERS_MAX;
    return s->players;
}

void rc_settings_cycle_players(rc_settings_t *s, int dir) {
    const uint8_t span = (uint8_t)(RC_PLAYERS_MAX - RC_PLAYERS_MIN + 1u);
    const uint8_t idx = (uint8_t)(rc_settings_players(s) - RC_PLAYERS_MIN);
    s->players = (uint8_t)(RC_PLAYERS_MIN + rc_wrap_step(idx, dir, span));
}

void rc_settings_cycle_limit(rc_settings_t *s, int dir) {
    s->limit_idx = rc_wrap_step(s->limit_idx, dir, (uint8_t)RC_LIMIT_OPTION_COUNT);
}

void rc_settings_cycle_nickname(rc_settings_t *s, uint8_t seat, int dir) {
    if (seat >= RC_SEATS || dir == 0) {
        return;
    }
    uint8_t cur = s->seat[seat].nickname_id;
    uint8_t next = rc_wrap_step(cur, dir, (uint8_t)RC_NICKNAME_COUNT);
    /* Swap with whichever seat holds `next`, so the ids stay a permutation. */
    for (uint8_t i = 0u; i < RC_SEATS; i++) {
        if (i != seat && s->seat[i].nickname_id == next) {
            s->seat[i].nickname_id = cur;
        }
    }
    s->seat[seat].nickname_id = next;
}

void rc_settings_cycle_faction(rc_settings_t *s, uint8_t seat, int dir) {
    if (seat >= RC_SEATS) {
        return;
    }
    s->seat[seat].faction_id =
        rc_wrap_step(s->seat[seat].faction_id, dir, (uint8_t)RC_FACTION_COUNT);
}

/* ---- SettingsMenu ---- */

void rc_menu_reset(rc_menu_t *m) {
    m->row = 0u;
}

void rc_menu_move(rc_menu_t *m, int dir) {
    m->row = rc_wrap_step(m->row, dir, (uint8_t)RC_MENU_ROWS);
}

void rc_menu_change(const rc_menu_t *m, rc_settings_t *s, int dir) {
    if (m->row >= RC_MENU_ROWS) {
        return;
    }
    switch (rc_menu_row_kind(m->row)) {
    case RC_ROW_PLAYERS:
        rc_settings_cycle_players(s, dir);
        break;
    case RC_ROW_LIMIT:
        rc_settings_cycle_limit(s, dir);
        break;
    case RC_ROW_NICKNAME:
        rc_settings_cycle_nickname(s, rc_menu_row_seat(m->row), dir);
        break;
    case RC_ROW_FACTION:
        rc_settings_cycle_faction(s, rc_menu_row_seat(m->row), dir);
        break;
    default:
        break;
    }
}

rc_row_kind_t rc_menu_row_kind(uint8_t row) {
    if (row == 0u) {
        return RC_ROW_PLAYERS;
    }
    if (row < RC_MENU_FIRST_SEAT_ROW || row >= RC_MENU_ROWS) {
        return RC_ROW_LIMIT;   /* row 1, or out of range: the seatless kind */
    }
    return ((row - RC_MENU_FIRST_SEAT_ROW) & 1u) ? RC_ROW_FACTION
                                                 : RC_ROW_NICKNAME;
}

uint8_t rc_menu_row_seat(uint8_t row) {
    if (row < RC_MENU_FIRST_SEAT_ROW || row >= RC_MENU_ROWS) {
        return RC_NO_SEAT;
    }
    return (uint8_t)((row - RC_MENU_FIRST_SEAT_ROW) / 2u);
}
