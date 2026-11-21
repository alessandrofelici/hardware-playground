/* rootclock -- Settings (what S2 edits) and SettingsMenu (S2's cursor).
 *
 * Defaults (documents/01 G9/G10, confirmed):
 *   4 players; limit Off; P1..P4 nicknames Jacob, Alex, Ahmed, Lorenzo;
 *   P1..P4 factions Marquise, Eyrie, Woodland, Vagabond.
 *
 * `players` (3 or 4) is how many seats the green "next" button rotates
 * through: P1 -> P2 -> P3 (-> P4) -> P1.
 *
 * Nicknames are unique across the four seats at all times: cycling a seat
 * onto a name another seat holds SWAPS the two (so the ids stay a
 * permutation of 0..3). Factions are not unique.
 *
 * Limit options, by index: Off, 0:30, 1:00, 1:30, 2:00, 3:00, 4:00, 5:00,
 * 7:00, 10:00, 15:00. All cycling wraps in both directions. */
#ifndef RC_SETTINGS_H
#define RC_SETTINGS_H
#include "rc_types.h"

#define RC_LIMIT_OPTION_COUNT 11u
#define RC_PLAYERS_MIN        3u
#define RC_PLAYERS_MAX        4u     /* == RC_SEATS */
#define RC_PLAYERS_DEFAULT    4u

typedef struct {
    uint8_t nickname_id;   /* 0..RC_NICKNAME_COUNT-1 */
    uint8_t faction_id;    /* rc_faction_id_t        */
} rc_seat_cfg_t;

typedef struct {
    uint8_t       players;            /* RC_PLAYERS_MIN..RC_PLAYERS_MAX     */
    uint8_t       limit_idx;          /* 0..RC_LIMIT_OPTION_COUNT-1; 0 = Off */
    rc_seat_cfg_t seat[RC_SEATS];
} rc_settings_t;

void rc_settings_defaults(rc_settings_t *s);

/* The rotation size, clamped to RC_PLAYERS_MIN..RC_PLAYERS_MAX. */
uint8_t rc_settings_players(const rc_settings_t *s);

/* 3 <-> 4; wraps in both directions. */
void rc_settings_cycle_players(rc_settings_t *s, int dir);

/* Milliseconds for limit option `idx`; 0 for Off or an out-of-range idx. */
uint32_t rc_limit_option_ms(uint8_t idx);

/* The current per-turn limit in ms; 0 = off. */
uint32_t rc_settings_limit_ms(const rc_settings_t *s);

/* dir is +1 or -1; anything > 0 counts as +1, anything < 0 as -1, 0 no-op. */
void rc_settings_cycle_limit(rc_settings_t *s, int dir);
void rc_settings_cycle_nickname(rc_settings_t *s, uint8_t seat, int dir);
void rc_settings_cycle_faction(rc_settings_t *s, uint8_t seat, int dir);

/* ---- SettingsMenu: 10 rows ----
 *   row 0: PLAYERS
 *   row 1: LIMIT
 *   row 2: P1 nickname   row 3: P1 faction
 *   row 4: P2 nickname   row 5: P2 faction
 *   row 6: P3 nickname   row 7: P3 faction
 *   row 8: P4 nickname   row 9: P4 faction                                  */
#define RC_MENU_ROWS 10u
#define RC_MENU_FIRST_SEAT_ROW 2u

typedef enum {
    RC_ROW_LIMIT = 0,
    RC_ROW_NICKNAME,
    RC_ROW_FACTION,
    RC_ROW_PLAYERS
} rc_row_kind_t;

typedef struct {
    uint8_t row;   /* 0..RC_MENU_ROWS-1 */
} rc_menu_t;

/* Cursor to row 0. */
void rc_menu_reset(rc_menu_t *m);

/* Move the cursor; wraps (up from row 0 goes to row 9 and back). */
void rc_menu_move(rc_menu_t *m, int dir);

/* Change the value on the cursor's row by `dir` (cycle_players / limit /
 * nickname / faction for the row's seat). */
void rc_menu_change(const rc_menu_t *m, rc_settings_t *s, int dir);

/* Row 0 PLAYERS, row 1 LIMIT, seat rows as above; out of range -> LIMIT
 * (a seatless kind; rc_menu_change ignores out-of-range rows). */
rc_row_kind_t rc_menu_row_kind(uint8_t row);

/* Seat 0..3 for rows 2..9; RC_NO_SEAT for rows 0-1 or out of range. */
uint8_t rc_menu_row_seat(uint8_t row);

#endif
