/* rootclock -- InputMapper. See rc_input.h for the button map and the rules.
 *
 * Per call, in this order:
 *   1. cancel an armed hold if its state no longer holds or blue is up;
 *   2. take the lowest-id press edge (others in the same poll are ignored),
 *      map it to its tap action, and let a blue edge in PAUSED/OVER arm the
 *      hold;
 *   3. only if step 2 returned no action, fire an armed hold that has been
 *      down for RC_HOLD_MS. */
#include "rc_input.h"

#define RC_BLUE_BIT RC_BTN_BIT(RC_BTN_BLUE)

static rc_action_t rc_act(rc_action_kind_t kind, uint8_t seat) {
    rc_action_t a = { kind, seat };
    return a;
}

/* The tap action for one press edge of `btn` in `state` (the table in
 * rc_input.h). Holds are not handled here. */
static rc_action_t rc_tap(rc_state_t state, rc_btn_t btn) {
    switch (state) {
    case RC_STATE_READY:
        if (btn == RC_BTN_YELLOW) return rc_act(RC_ACT_OPEN_SETTINGS, RC_NO_SEAT);
        if (btn == RC_BTN_RED)    return rc_act(RC_ACT_START, RC_NO_SEAT);
        break;
    case RC_STATE_RUNNING:
        if (btn == RC_BTN_GREEN)  return rc_act(RC_ACT_NEXT, RC_NO_SEAT);
        if (btn == RC_BTN_RED)    return rc_act(RC_ACT_PAUSE, RC_NO_SEAT);
        break;
    case RC_STATE_PAUSED:
        if (btn == RC_BTN_YELLOW) return rc_act(RC_ACT_OPEN_SETTINGS, RC_NO_SEAT);
        if (btn == RC_BTN_GREEN)  return rc_act(RC_ACT_VIEW_NEXT, RC_NO_SEAT);
        if (btn == RC_BTN_RED)    return rc_act(RC_ACT_RESUME, RC_NO_SEAT);
        break;   /* blue only arms the END_GAME hold */
    case RC_STATE_SETTINGS:
        switch (btn) {
        case RC_BTN_GRAY:   return rc_act(RC_ACT_MENU_UP, RC_NO_SEAT);
        case RC_BTN_YELLOW: return rc_act(RC_ACT_MENU_PREV, RC_NO_SEAT);
        case RC_BTN_GREEN:  return rc_act(RC_ACT_MENU_DONE, RC_NO_SEAT);
        case RC_BTN_BLUE:   return rc_act(RC_ACT_MENU_NEXT, RC_NO_SEAT);
        case RC_BTN_RED:    return rc_act(RC_ACT_MENU_DOWN, RC_NO_SEAT);
        default:            break;
        }
        break;
    case RC_STATE_OVER:
        if (btn == RC_BTN_YELLOW) return rc_act(RC_ACT_SEND, RC_NO_SEAT);
        if (btn == RC_BTN_GREEN)  return rc_act(RC_ACT_VIEW_NEXT, RC_NO_SEAT);
        break;   /* blue only arms the NEW_GAME hold */
    default:
        break;
    }
    return rc_act(RC_ACT_NONE, RC_NO_SEAT);
}

void rc_input_init(rc_input_t *in) {
    in->blue_pressed_ms = 0u;
    in->hold_armed = false;
    in->hold_state = RC_STATE_READY;
}

rc_action_t rc_input_map(rc_input_t *in, rc_state_t state, rc_buttons_t b,
                         uint32_t now_ms) {
    rc_action_t act = rc_act(RC_ACT_NONE, RC_NO_SEAT);

    /* 1. Cancel: blue released, or the state moved on from where it armed. */
    if (in->hold_armed &&
        (state != in->hold_state || (b.released & RC_BLUE_BIT) != 0u ||
         (b.down & RC_BLUE_BIT) == 0u)) {
        in->hold_armed = false;
    }

    /* 2. The lowest-id press edge, if any. */
    for (unsigned i = 0u; i < (unsigned)RC_BTN_COUNT; i++) {
        rc_btn_t btn = (rc_btn_t)i;
        if ((b.pressed & RC_BTN_BIT(btn)) == 0u) {
            continue;
        }
        act = rc_tap(state, btn);
        if (btn == RC_BTN_BLUE &&
            (state == RC_STATE_PAUSED || state == RC_STATE_OVER)) {
            in->hold_armed = true;
            in->hold_state = state;
            in->blue_pressed_ms = now_ms;
        }
        break;
    }
    if (act.kind != RC_ACT_NONE) {
        return act;
    }

    /* 3. The hold, only when no tap action was returned this call. */
    if (in->hold_armed && state == in->hold_state &&
        (b.down & RC_BLUE_BIT) != 0u &&
        (uint32_t)(now_ms - in->blue_pressed_ms) >= RC_HOLD_MS) {
        in->hold_armed = false;   /* fires once */
        act = rc_act(in->hold_state == RC_STATE_PAUSED ? RC_ACT_END_GAME
                                                       : RC_ACT_NEW_GAME,
                     RC_NO_SEAT);
    }
    return act;
}

unsigned rc_input_hold_progress(const rc_input_t *in, rc_state_t state,
                                uint32_t now_ms) {
    if (!in->hold_armed || state != in->hold_state) {
        return 0u;
    }
    uint32_t held = now_ms - in->blue_pressed_ms;
    if (held >= RC_HOLD_MS) {
        return 100u;
    }
    return (unsigned)(held * 100u / RC_HOLD_MS);
}
