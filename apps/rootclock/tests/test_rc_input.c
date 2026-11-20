/* rootclock host tests -- InputMapper: the button map and the blue holds. */
#include "rc_test_support.h"

#define GRAY   RC_BTN_BIT(RC_BTN_GRAY)
#define YELLOW RC_BTN_BIT(RC_BTN_YELLOW)
#define GREEN  RC_BTN_BIT(RC_BTN_GREEN)
#define BLUE   RC_BTN_BIT(RC_BTN_BLUE)
#define RED    RC_BTN_BIT(RC_BTN_RED)

/* A press edge of `mask` with `mask | also_down` held. */
static rc_buttons_t press(uint8_t mask, uint8_t also_down) {
    return rc_t_btn((uint8_t)(mask | also_down), mask, 0u);
}
/* No edges, `mask` held. */
static rc_buttons_t held(uint8_t mask) {
    return rc_t_btn(mask, 0u, 0u);
}
/* A release edge of `mask`, `still_down` held. */
static rc_buttons_t release(uint8_t mask, uint8_t still_down) {
    return rc_t_btn(still_down, 0u, mask);
}

/* ASSERT_ACT(action, kind, seat). The variadic hop lets NONE_ (kind and seat
 * together) expand into two arguments before they are counted. */
#define ASSERT_ACT(...) ASSERT_ACT_(__VA_ARGS__)
#define ASSERT_ACT_(a, k, s) do { \
    rc_action_t _x = (a); \
    ASSERT_EQ(_x.kind, (k)); \
    ASSERT_EQ(_x.seat, (s)); \
} while (0)
#define NONE_ RC_ACT_NONE, RC_NO_SEAT

/* Every cell of the button map in rc_input.h / documents/01 section 3. */
static void test_button_map(void) {
    static const struct {
        rc_state_t       state;
        rc_btn_t         btn;
        rc_action_kind_t kind;
        uint8_t          seat;
        bool             arms;
    } rows[] = {
        { RC_STATE_READY,    RC_BTN_GRAY,   RC_ACT_NONE,          RC_NO_SEAT, false },
        { RC_STATE_READY,    RC_BTN_YELLOW, RC_ACT_OPEN_SETTINGS, RC_NO_SEAT, false },
        { RC_STATE_READY,    RC_BTN_GREEN,  RC_ACT_NONE,          RC_NO_SEAT, false },
        { RC_STATE_READY,    RC_BTN_BLUE,   RC_ACT_NONE,          RC_NO_SEAT, false },
        { RC_STATE_READY,    RC_BTN_RED,    RC_ACT_START,         RC_NO_SEAT, false },

        { RC_STATE_RUNNING,  RC_BTN_GRAY,   RC_ACT_SWITCH,        0u,         false },
        { RC_STATE_RUNNING,  RC_BTN_YELLOW, RC_ACT_SWITCH,        1u,         false },
        { RC_STATE_RUNNING,  RC_BTN_GREEN,  RC_ACT_SWITCH,        2u,         false },
        { RC_STATE_RUNNING,  RC_BTN_BLUE,   RC_ACT_SWITCH,        3u,         false },
        { RC_STATE_RUNNING,  RC_BTN_RED,    RC_ACT_PAUSE,         RC_NO_SEAT, false },

        { RC_STATE_PAUSED,   RC_BTN_GRAY,   RC_ACT_NONE,          RC_NO_SEAT, false },
        { RC_STATE_PAUSED,   RC_BTN_YELLOW, RC_ACT_OPEN_SETTINGS, RC_NO_SEAT, false },
        { RC_STATE_PAUSED,   RC_BTN_GREEN,  RC_ACT_NONE,          RC_NO_SEAT, false },
        { RC_STATE_PAUSED,   RC_BTN_BLUE,   RC_ACT_NONE,          RC_NO_SEAT, true  },
        { RC_STATE_PAUSED,   RC_BTN_RED,    RC_ACT_RESUME,        RC_NO_SEAT, false },

        { RC_STATE_SETTINGS, RC_BTN_GRAY,   RC_ACT_MENU_UP,       RC_NO_SEAT, false },
        { RC_STATE_SETTINGS, RC_BTN_YELLOW, RC_ACT_MENU_PREV,     RC_NO_SEAT, false },
        { RC_STATE_SETTINGS, RC_BTN_GREEN,  RC_ACT_MENU_DONE,     RC_NO_SEAT, false },
        { RC_STATE_SETTINGS, RC_BTN_BLUE,   RC_ACT_MENU_NEXT,     RC_NO_SEAT, false },
        { RC_STATE_SETTINGS, RC_BTN_RED,    RC_ACT_MENU_DOWN,     RC_NO_SEAT, false },

        { RC_STATE_OVER,     RC_BTN_GRAY,   RC_ACT_VIEW,          0u,         false },
        { RC_STATE_OVER,     RC_BTN_YELLOW, RC_ACT_VIEW,          1u,         false },
        { RC_STATE_OVER,     RC_BTN_GREEN,  RC_ACT_VIEW,          2u,         false },
        { RC_STATE_OVER,     RC_BTN_BLUE,   RC_ACT_VIEW,          3u,         true  },
        { RC_STATE_OVER,     RC_BTN_RED,    RC_ACT_NONE,          RC_NO_SEAT, false },
    };
    for (unsigned i = 0u; i < sizeof rows / sizeof rows[0]; i++) {
        int before = g_failures;
        rc_input_t in;
        uint8_t bit = RC_BTN_BIT(rows[i].btn);

        /* The press edge maps to the cell's action. */
        rc_input_init(&in);
        ASSERT_ACT(rc_input_map(&in, rows[i].state, press(bit, 0u), 5000u),
                   rows[i].kind, rows[i].seat);
        ASSERT_EQ(in.hold_armed, rows[i].arms);

        /* Holding it without a new edge does not repeat the tap; letting go
         * does nothing either (taps act on press). */
        rc_input_init(&in);
        ASSERT_ACT(rc_input_map(&in, rows[i].state, held(bit), 6000u), NONE_);
        ASSERT_ACT(rc_input_map(&in, rows[i].state, release(bit, 0u), 6100u),
                   NONE_);
        ASSERT_TRUE(!in.hold_armed);

        RC_CONTEXT_ON_FAIL(before, "button map row %u (state %d, button %d)", i,
                           (int)rows[i].state, (int)rows[i].btn);
    }

    /* No buttons at all: nothing, in every state. */
    for (int st = RC_STATE_READY; st <= RC_STATE_OVER; st++) {
        rc_input_t in;
        rc_input_init(&in);
        ASSERT_ACT(rc_input_map(&in, (rc_state_t)st, held(0u), 1u), NONE_);
    }
}

/* Several press edges in one poll: the lowest button id wins, the others are
 * ignored entirely (including a blue edge's hold arming). */
static void test_simultaneous_edges(void) {
    rc_input_t in;

    rc_input_init(&in);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_RUNNING, press(GRAY | GREEN, 0u), 1u),
               RC_ACT_SWITCH, 0u);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_RUNNING, press(YELLOW | BLUE, 0u), 2u),
               RC_ACT_SWITCH, 1u);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_RUNNING, press(BLUE | RED, 0u), 3u),
               RC_ACT_SWITCH, 3u);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_RUNNING,
                            press(GRAY | YELLOW | GREEN | BLUE | RED, 0u), 4u),
               RC_ACT_SWITCH, 0u);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_READY, press(YELLOW | RED, 0u), 5u),
               RC_ACT_OPEN_SETTINGS, RC_NO_SEAT);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_SETTINGS, press(GREEN | BLUE, 0u), 6u),
               RC_ACT_MENU_DONE, RC_NO_SEAT);

    /* The winner decides even when its own cell is empty: green beats red
     * in READY, so this poll does not START. */
    ASSERT_ACT(rc_input_map(&in, RC_STATE_READY, press(GREEN | RED, 0u), 7u),
               NONE_);

    /* Gray beats blue in OVER: VIEW 0, and blue's edge does not arm. */
    rc_input_init(&in);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_OVER, press(GRAY | BLUE, 0u), 100u),
               RC_ACT_VIEW, 0u);
    ASSERT_TRUE(!in.hold_armed);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_OVER, held(GRAY | BLUE), 5000u), NONE_);

    /* Yellow beats blue in PAUSED: settings, no END_GAME hold. */
    rc_input_init(&in);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, press(YELLOW | BLUE, 0u), 100u),
               RC_ACT_OPEN_SETTINGS, RC_NO_SEAT);
    ASSERT_TRUE(!in.hold_armed);
}

static void test_hold_end_game(void) {
    rc_input_t in;
    const uint32_t t0 = 10000u;

    rc_input_init(&in);
    ASSERT_EQ(rc_input_hold_progress(&in, RC_STATE_PAUSED, t0), 0u);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, press(BLUE, 0u), t0), NONE_);
    ASSERT_TRUE(in.hold_armed);

    ASSERT_EQ(rc_input_hold_progress(&in, RC_STATE_PAUSED, t0), 0u);
    ASSERT_EQ(rc_input_hold_progress(&in, RC_STATE_PAUSED, t0 + 500u), 25u);
    ASSERT_EQ(rc_input_hold_progress(&in, RC_STATE_PAUSED, t0 + 1000u), 50u);
    ASSERT_EQ(rc_input_hold_progress(&in, RC_STATE_PAUSED, t0 + 1999u), 99u);
    ASSERT_EQ(rc_input_hold_progress(&in, RC_STATE_PAUSED, t0 + 2000u), 100u);
    ASSERT_EQ(rc_input_hold_progress(&in, RC_STATE_PAUSED, t0 + 99000u), 100u);
    /* Only in the state it was armed in. */
    ASSERT_EQ(rc_input_hold_progress(&in, RC_STATE_RUNNING, t0 + 1000u), 0u);
    ASSERT_EQ(rc_input_hold_progress(&in, RC_STATE_OVER, t0 + 1000u), 0u);

    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, held(BLUE), t0 + 1000u), NONE_);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, held(BLUE), t0 + 1999u), NONE_);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, held(BLUE), t0 + 2000u),
               RC_ACT_END_GAME, RC_NO_SEAT);
    ASSERT_TRUE(!in.hold_armed);
    ASSERT_EQ(rc_input_hold_progress(&in, RC_STATE_PAUSED, t0 + 2001u), 0u);

    /* Fires once: still holding gives nothing more, in PAUSED or in OVER (a
     * NEW_GAME hold needs its own blue press in OVER). */
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, held(BLUE), t0 + 2001u), NONE_);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_OVER, held(BLUE), t0 + 2002u), NONE_);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_OVER, held(BLUE), t0 + 9000u), NONE_);
    ASSERT_EQ(rc_input_hold_progress(&in, RC_STATE_OVER, t0 + 9000u), 0u);
}

static void test_hold_new_game_across_wrap(void) {
    rc_input_t in;
    const uint32_t t0 = 0xFFFFFC18u;    /* 1000 ms before the uint32 wrap */

    rc_input_init(&in);
    /* A blue press in OVER is VIEW 3 at once AND arms NEW_GAME. */
    ASSERT_ACT(rc_input_map(&in, RC_STATE_OVER, press(BLUE, 0u), t0),
               RC_ACT_VIEW, 3u);
    ASSERT_TRUE(in.hold_armed);
    ASSERT_EQ(rc_input_hold_progress(&in, RC_STATE_OVER, 0u), 50u);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_OVER, held(BLUE), t0 + 1999u), NONE_);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_OVER, held(BLUE), t0 + 2000u),
               RC_ACT_NEW_GAME, RC_NO_SEAT);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_READY, held(BLUE), t0 + 2500u), NONE_);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_READY, held(BLUE), t0 + 9000u), NONE_);
}

static void test_hold_cancel_release(void) {
    rc_input_t in;

    rc_input_init(&in);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, press(BLUE, 0u), 1000u), NONE_);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, held(BLUE), 2500u), NONE_);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, release(BLUE, 0u), 2900u), NONE_);
    ASSERT_TRUE(!in.hold_armed);
    ASSERT_EQ(rc_input_hold_progress(&in, RC_STATE_PAUSED, 2900u), 0u);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, held(0u), 4000u), NONE_);

    /* A new press re-arms from its own edge. */
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, press(BLUE, 0u), 5000u), NONE_);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, held(BLUE), 6999u), NONE_);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, held(BLUE), 7000u),
               RC_ACT_END_GAME, RC_NO_SEAT);

    /* Blue reported up without a release edge also cancels. */
    rc_input_init(&in);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, press(BLUE, 0u), 10000u), NONE_);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, held(GRAY), 10500u), NONE_);
    ASSERT_TRUE(!in.hold_armed);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, held(BLUE), 13000u), NONE_);

    /* Releasing another button does not cancel it. */
    rc_input_init(&in);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_OVER, press(BLUE, GRAY), 20000u),
               RC_ACT_VIEW, 3u);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_OVER, release(GRAY, BLUE), 20500u),
               NONE_);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_OVER, held(BLUE), 22000u),
               RC_ACT_NEW_GAME, RC_NO_SEAT);
}

static void test_hold_cancel_state(void) {
    rc_input_t in;

    /* Armed in PAUSED, then the state moves on: cancelled for good, even if
     * the state comes back while blue is still down. */
    static const rc_state_t elsewhere[] = {
        RC_STATE_READY, RC_STATE_RUNNING, RC_STATE_SETTINGS, RC_STATE_OVER,
    };
    for (unsigned i = 0u; i < sizeof elsewhere / sizeof elsewhere[0]; i++) {
        int before = g_failures;
        rc_input_init(&in);
        ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, press(BLUE, 0u), 1000u),
                   NONE_);
        ASSERT_ACT(rc_input_map(&in, elsewhere[i], held(BLUE), 1500u), NONE_);
        ASSERT_TRUE(!in.hold_armed);
        ASSERT_ACT(rc_input_map(&in, elsewhere[i], held(BLUE), 3500u), NONE_);
        ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, held(BLUE), 4000u), NONE_);
        RC_CONTEXT_ON_FAIL(before, "cancel by state %d", (int)elsewhere[i]);
    }

    /* Armed in OVER, then READY: no NEW_GAME. */
    rc_input_init(&in);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_OVER, press(BLUE, 0u), 1000u),
               RC_ACT_VIEW, 3u);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_READY, held(BLUE), 3500u), NONE_);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_OVER, held(BLUE), 4000u), NONE_);
}

/* Blue edges outside PAUSED/OVER never arm, even if the state later becomes
 * PAUSED with blue still held. */
static void test_hold_only_armed_in_paused_or_over(void) {
    rc_input_t in;
    static const rc_state_t states[] = {
        RC_STATE_READY, RC_STATE_RUNNING, RC_STATE_SETTINGS,
    };
    for (unsigned i = 0u; i < sizeof states / sizeof states[0]; i++) {
        int before = g_failures;
        rc_input_init(&in);
        (void)rc_input_map(&in, states[i], press(BLUE, 0u), 1000u);
        ASSERT_TRUE(!in.hold_armed);
        ASSERT_ACT(rc_input_map(&in, states[i], held(BLUE), 5000u), NONE_);
        ASSERT_EQ(rc_input_hold_progress(&in, states[i], 5000u), 0u);
        RC_CONTEXT_ON_FAIL(before, "no arm in state %d", (int)states[i]);
    }

    /* Blue down for P4's turn, red pauses while blue is still held: PAUSED
     * never saw a blue press edge, so no END_GAME. */
    rc_input_init(&in);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_RUNNING, press(BLUE, 0u), 1000u),
               RC_ACT_SWITCH, 3u);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_RUNNING, press(RED, BLUE), 1200u),
               RC_ACT_PAUSE, RC_NO_SEAT);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, held(BLUE), 5000u), NONE_);
    ASSERT_EQ(rc_input_hold_progress(&in, RC_STATE_PAUSED, 5000u), 0u);
}

/* A tap returned in a call means the hold is not evaluated in that call. */
static void test_tap_preempts_hold(void) {
    rc_input_t in;

    /* OVER: gray while holding blue past 2 s is VIEW 0; the hold, not
     * cancelled by a tap, fires on the next call. */
    rc_input_init(&in);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_OVER, press(BLUE, 0u), 1000u),
               RC_ACT_VIEW, 3u);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_OVER, press(GRAY, BLUE), 3500u),
               RC_ACT_VIEW, 0u);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_OVER, held(GRAY | BLUE), 3501u),
               RC_ACT_NEW_GAME, RC_NO_SEAT);

    /* PAUSED: red while holding blue past 2 s is RESUME, not END_GAME; the
     * game is then RUNNING, which cancels the hold. */
    rc_input_init(&in);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, press(BLUE, 0u), 1000u), NONE_);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, press(RED, BLUE), 3500u),
               RC_ACT_RESUME, RC_NO_SEAT);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_RUNNING, held(RED | BLUE), 3501u), NONE_);
    ASSERT_TRUE(!in.hold_armed);

    /* PAUSED: gray has no tap there, so the same call does evaluate the hold. */
    rc_input_init(&in);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, press(BLUE, 0u), 1000u), NONE_);
    ASSERT_ACT(rc_input_map(&in, RC_STATE_PAUSED, press(GRAY, BLUE), 3500u),
               RC_ACT_END_GAME, RC_NO_SEAT);
}

int main(void) {
    test_button_map();
    test_simultaneous_edges();
    test_hold_end_game();
    test_hold_new_game_across_wrap();
    test_hold_cancel_release();
    test_hold_cancel_state();
    test_hold_only_armed_in_paused_or_over();
    test_tap_preempts_hold();
    TEST_RETURN();
}
