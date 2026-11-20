/* rootclock -- Settings (what S2 edits) and SettingsMenu (S2's cursor).
 *
 * Defaults (documents/01 G9/G10, confirmed):
 *   limit Off; P1..P4 nicknames Jacob, Alex, Ahmed, Lorenzo;
 *   P1..P4 factions Marquise, Eyrie, Woodland, Vagabond.
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

typedef struct {
    uint8_t nickname_id;   /* 0..RC_NICKNAME_COUNT-1 */
    uint8_t faction_id;    /* rc_faction_id_t        */
} rc_seat_cfg_t;

typedef struct {
    uint8_t       limit_idx;          /* 0..RC_LIMIT_OPTION_COUNT-1; 0 = Off */
    rc_seat_cfg_t seat[RC_SEATS];
} rc_settings_t;

void rc_settings_defaults(rc_settings_t *s);

/* Milliseconds for limit option `idx`; 0 for Off or an out-of-range idx. */
uint32_t rc_limit_option_ms(uint8_t idx);

/* The current per-turn limit in ms; 0 = off. */
uint32_t rc_settings_limit_ms(const rc_settings_t *s);

/* dir is +1 or -1; anything > 0 counts as +1, anything < 0 as -1, 0 no-op. */
void rc_settings_cycle_limit(rc_settings_t *s, int dir);
void rc_settings_cycle_nickname(rc_settings_t *s, uint8_t seat, int dir);
void rc_settings_cycle_faction(rc_settings_t *s, uint8_t seat, int dir);

/* ---- SettingsMenu: 9 rows ----
 *   row 0: LIMIT
 *   row 1: P1 nickname   row 2: P1 faction
 *   row 3: P2 nickname   row 4: P2 faction
 *   row 5: P3 nickname   row 6: P3 faction
 *   row 7: P4 nickname   row 8: P4 faction                                  */
#define RC_MENU_ROWS 9u

typedef enum {
    RC_ROW_LIMIT = 0,
    RC_ROW_NICKNAME,
    RC_ROW_FACTION
} rc_row_kind_t;

typedef struct {
    uint8_t row;   /* 0..RC_MENU_ROWS-1 */
} rc_menu_t;

/* Cursor to row 0. */
void rc_menu_reset(rc_menu_t *m);

/* Move the cursor; wraps (up from row 0 goes to row 8 and back). */
void rc_menu_move(rc_menu_t *m, int dir);

/* Change the value on the cursor's row by `dir` (cycle_limit / nickname /
 * faction for the row's seat). */
void rc_menu_change(const rc_menu_t *m, rc_settings_t *s, int dir);

rc_row_kind_t rc_menu_row_kind(uint8_t row);

/* Seat 0..3 for rows 1..8; RC_NO_SEAT for row 0 or out of range. */
uint8_t rc_menu_row_seat(uint8_t row);

#endif
