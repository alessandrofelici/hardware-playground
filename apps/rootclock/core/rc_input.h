/* rootclock -- InputMapper: one poll of buttons -> at most one action.
 *
 * The button map (documents/01 section 3, confirmed):
 *
 *   state     gray      yellow         green     blue                  red
 *   READY     -         OPEN_SETTINGS  -         -                     START
 *   RUNNING   SWITCH 0  SWITCH 1       SWITCH 2  SWITCH 3              PAUSE
 *   PAUSED    -         OPEN_SETTINGS  -         hold: END_GAME        RESUME
 *   SETTINGS  MENU_UP   MENU_PREV      MENU_DONE MENU_NEXT             MENU_DOWN
 *   OVER      VIEW 0    VIEW 1         VIEW 2    VIEW 3, hold: NEW_GAME -
 *
 * Rules:
 *   - Taps act on the PRESS edge (b.pressed), not on release.
 *   - If several press edges arrive in one poll, the lowest rc_btn_t wins and
 *     the others are ignored.
 *   - A hold (blue, RC_HOLD_MS) is ARMED only by a blue press edge that
 *     arrives while state is PAUSED or OVER. It fires once, on the first call
 *     where blue is still down and now_ms - press_ms >= RC_HOLD_MS, returning
 *     END_GAME (armed in PAUSED) or NEW_GAME (armed in OVER). It is cancelled
 *     by blue's release, by firing, or by `state` differing from the state it
 *     was armed in. A blue press in OVER returns VIEW 3 immediately AND arms
 *     the NEW_GAME hold.
 *   - Red is never held for anything: the BSP owns red-held-6-s (power off).
 *   - A press edge returns its tap action even while a hold is armed; the
 *     hold is evaluated only when no tap action was returned this call.
 *
 * Pure: the caller passes rc_buttons_t built from fwog_power_poll()'s result. */
#ifndef RC_INPUT_H
#define RC_INPUT_H
#include "rc_types.h"

typedef struct {
    uint32_t   blue_pressed_ms;  /* press edge time of the armed hold      */
    bool       hold_armed;
    rc_state_t hold_state;       /* state the hold was armed in            */
} rc_input_t;

void rc_input_init(rc_input_t *in);

rc_action_t rc_input_map(rc_input_t *in, rc_state_t state, rc_buttons_t b,
                         uint32_t now_ms);

/* 0..100: progress of an armed hold that is valid in `state`; 0 otherwise.
 * Drives the "keep holding..." hint. */
unsigned rc_input_hold_progress(const rc_input_t *in, rc_state_t state,
                                uint32_t now_ms);

#endif
