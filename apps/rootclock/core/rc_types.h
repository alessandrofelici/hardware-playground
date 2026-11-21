/* rootclock -- shared types and constants for the SDK-free core.
 *
 * Everything under core/ is pure C11: no Pico SDK or BSP includes, time
 * arrives as a `now_ms` argument (any monotonic millisecond clock; compare
 * with unsigned subtraction so the uint32 wrap is harmless), and buttons
 * arrive as rc_buttons_t. That is what makes core/ host-testable.
 *
 * Seats are 0-based everywhere in code (0..3) and shown/exported as 1..4.
 * See documents/03-object-model.md for the model and the invariants. */
#ifndef RC_TYPES_H
#define RC_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RC_SEATS      4u      /* players 1..4 -> seats 0..3                 */
#define RC_MAX_TURNS  128u    /* per-player turn log capacity               */
#define RC_HOLD_MS    2000u   /* blue hold for END_GAME / NEW_GAME          */
#define RC_NO_SEAT    0xFFu   /* "no seat" for active/viewed seat fields    */
#define RC_APP_VERSION "003"  /* must match VERSION in apps/rootclock/CMakeLists.txt */

/* ---- Game state (documents/03 state diagram) ---- */
typedef enum {
    RC_STATE_READY = 0,   /* boot / after NEW_GAME: clocks zero, waiting for START */
    RC_STATE_RUNNING,     /* active seat's turn is counting                        */
    RC_STATE_PAUSED,      /* everything frozen; menu: resume/settings/end          */
    RC_STATE_SETTINGS,    /* S2 open; return_state says where MENU_DONE goes       */
    RC_STATE_OVER         /* game ended; clocks frozen; cards viewable             */
} rc_state_t;

/* ---- Buttons: same ids and bit order as the BSP's fwog_btn_id_t
 * (gray=0x01, yellow=0x02, green=0x04, blue=0x08, red=0x10). display/main.c
 * static_asserts the correspondence. ---- */
typedef enum {
    RC_BTN_GRAY = 0,
    RC_BTN_YELLOW,
    RC_BTN_GREEN,
    RC_BTN_BLUE,
    RC_BTN_RED,
    RC_BTN_COUNT
} rc_btn_t;

#define RC_BTN_BIT(b) ((uint8_t)(1u << (unsigned)(b)))

/* One poll's worth of button state, mirroring the BSP's fwog_buttons_t. */
typedef struct {
    uint8_t down;      /* bit per rc_btn_t: settled pressed          */
    uint8_t pressed;   /* bit per rc_btn_t: became pressed this poll */
    uint8_t released;  /* bit per rc_btn_t: became released this poll*/
} rc_buttons_t;

/* ---- Actions: what the user asked for, independent of which button ---- */
typedef enum {
    RC_ACT_NONE = 0,
    RC_ACT_START,          /* READY -> RUNNING, seat 0 starts             */
    RC_ACT_NEXT,           /* RUNNING: end current turn, begin the next
                              seat's in the rotation (green)              */
    RC_ACT_VIEW_NEXT,      /* PAUSED/OVER: show the next seat's card      */
    RC_ACT_SWITCH,         /* RUNNING: end current turn, begin `seat`'s
                              (API only; no button emits it since 002)    */
    RC_ACT_PAUSE,          /* RUNNING -> PAUSED                           */
    RC_ACT_RESUME,         /* PAUSED -> RUNNING, same turn continues      */
    RC_ACT_OPEN_SETTINGS,  /* READY|PAUSED -> SETTINGS                    */
    RC_ACT_END_GAME,       /* PAUSED -> OVER (blue held RC_HOLD_MS)       */
    RC_ACT_NEW_GAME,       /* OVER -> READY (blue held RC_HOLD_MS)        */
    RC_ACT_MENU_UP,        /* SETTINGS: cursor up (gray)                  */
    RC_ACT_MENU_DOWN,      /* SETTINGS: cursor down (red)                 */
    RC_ACT_MENU_PREV,      /* SETTINGS: value - (yellow)                  */
    RC_ACT_MENU_NEXT,      /* SETTINGS: value + (blue)                    */
    RC_ACT_MENU_DONE,      /* SETTINGS -> return_state (green)            */
    RC_ACT_VIEW            /* OVER: show `seat`'s final card (API only)   */
} rc_action_kind_t;

typedef struct {
    rc_action_kind_t kind;
    uint8_t seat;          /* SWITCH / VIEW target, 0..3; else RC_NO_SEAT */
} rc_action_t;

/* ---- Events returned by rc_game_apply() / rc_game_tick() (bit set) ---- */
typedef uint32_t rc_events_t;
#define RC_EVT_NONE       0u
#define RC_EVT_BEEP       (1u << 0)  /* the active turn just crossed the limit  */
#define RC_EVT_GAME_OVER  (1u << 1)  /* END_GAME applied: build + DIAG result   */

/* ---- Factions: ORDER IS LOAD-BEARING. It is the export key table order in
 * documents/04 section 2, the order of rc_factions[] in rc_catalog.c and the
 * order of the generated icons in display/assets/faction_icons.c. ---- */
typedef enum {
    RC_FACTION_MARQUISE = 0,
    RC_FACTION_EYRIE,
    RC_FACTION_WOODLAND,
    RC_FACTION_VAGABOND,
    RC_FACTION_RIVERFOLK,
    RC_FACTION_CULT,
    RC_FACTION_DUCHY,
    RC_FACTION_CORVID,
    RC_FACTION_HUNDREDS,
    RC_FACTION_KEEPERS,
    RC_FACTION_TWILIGHT,
    RC_FACTION_KNAVES,
    RC_FACTION_LILYPAD,
    RC_FACTION_COUNT
} rc_faction_id_t;

#define RC_NICKNAME_COUNT 4u   /* Jacob, Alex, Ahmed, Lorenzo */

#endif
