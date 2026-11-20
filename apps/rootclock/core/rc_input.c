/* rootclock -- InputMapper. STUB: implemented on feat/rootclock-core. */
#include "rc_input.h"

void rc_input_init(rc_input_t *in) { (void)in; }
rc_action_t rc_input_map(rc_input_t *in, rc_state_t state, rc_buttons_t b, uint32_t now_ms) {
    (void)in; (void)state; (void)b; (void)now_ms;
    rc_action_t a = { RC_ACT_NONE, RC_NO_SEAT };
    return a;
}
unsigned rc_input_hold_progress(const rc_input_t *in, rc_state_t state, uint32_t now_ms) {
    (void)in; (void)state; (void)now_ms; return 0u;
}
