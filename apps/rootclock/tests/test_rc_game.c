/* rootclock host tests -- GameClock: every transition, every ignored action,
 * the alarm in play, the documents/02 acceptance script (core side), and a
 * long randomized game driven through the InputMapper, with the documents/03
 * invariants checked after every step. */
#include "rc_test_support.h"

#include "rc_alarm.h"
#include "rc_settings.h"

/* ---- helpers ---- */

/* Through the menu, as a user would: S2 from READY or PAUSED, cursor to
 * LIMIT, step to option `idx`, done. */
static void set_limit(rc_game_t *g, uint8_t idx, uint32_t now) {
    rc_t_apply(g, ACT(OPEN_SETTINGS), now);
    ASSERT_EQ(g->state, RC_STATE_SETTINGS);
    ASSERT_EQ(g->menu.row, 0u);
    for (unsigned i = 0u;
         i < RC_LIMIT_OPTION_COUNT && g->settings.limit_idx != idx; i++) {
        rc_t_apply(g, ACT(MENU_NEXT), now);
    }
    ASSERT_EQ(g->settings.limit_idx, idx);
    rc_t_apply(g, ACT(MENU_DONE), now);
}

/* ---- every state, every action ---- */

typedef enum {
    SIT_READY,
    SIT_RUNNING,
    SIT_PAUSED,
    SIT_SETTINGS_FROM_READY,
    SIT_SETTINGS_FROM_PAUSED,
    SIT_OVER,
    SIT_COUNT
} sit_t;

/* A fresh game in `sit`, reached only through valid actions. Seat 1 is the
 * active one, so seat-relative cases are not all seat 0. Returns now. */
static uint32_t make_sit(rc_game_t *g, sit_t sit) {
    uint32_t t = 1000u;
    rc_game_init(g);
    if (sit == SIT_READY) {
        return t;
    }
    if (sit == SIT_SETTINGS_FROM_READY) {
        rc_game_apply(g, ACT(OPEN_SETTINGS), t);
        return t;
    }
    rc_game_apply(g, ACT(START), t);
    t += 5000u;
    rc_game_apply(g, ACTS(SWITCH, 1), t);
    t += 7000u;
    if (sit == SIT_RUNNING) {
        return t;
    }
    rc_game_apply(g, ACT(PAUSE), t);
    t += 3000u;
    if (sit == SIT_PAUSED) {
        return t;
    }
    if (sit == SIT_SETTINGS_FROM_PAUSED) {
        rc_game_apply(g, ACT(OPEN_SETTINGS), t);
        return t;
    }
    rc_game_apply(g, ACT(END_GAME), t);
    return t;
}

/* The rc_game.h transition table. SWITCH to the active seat is NOT valid:
 * it must change nothing (invariant 6), so the ignored-action test covers it. */
static bool is_valid(const rc_game_t *g, rc_action_t a) {
    switch (g->state) {
    case RC_STATE_READY:
        return a.kind == RC_ACT_START || a.kind == RC_ACT_OPEN_SETTINGS;
    case RC_STATE_RUNNING:
        return a.kind == RC_ACT_PAUSE ||
               (a.kind == RC_ACT_SWITCH && a.seat < RC_SEATS &&
                a.seat != g->active_seat);
    case RC_STATE_PAUSED:
        return a.kind == RC_ACT_RESUME || a.kind == RC_ACT_OPEN_SETTINGS ||
               a.kind == RC_ACT_END_GAME;
    case RC_STATE_SETTINGS:
        return a.kind == RC_ACT_MENU_UP || a.kind == RC_ACT_MENU_DOWN ||
               a.kind == RC_ACT_MENU_PREV || a.kind == RC_ACT_MENU_NEXT ||
               a.kind == RC_ACT_MENU_DONE;
    case RC_STATE_OVER:
        return (a.kind == RC_ACT_VIEW && a.seat < RC_SEATS) ||
               a.kind == RC_ACT_NEW_GAME;
    default:
        return false;
    }
}

static void test_ignored_actions(void) {
    static const uint8_t seats[] = { 0u, 1u, 2u, 3u, 4u, RC_NO_SEAT };
    static rc_game_t g, before;
    unsigned checked = 0u;

    for (int sit = 0; sit < SIT_COUNT; sit++) {
        for (int kind = RC_ACT_NONE; kind <= RC_ACT_VIEW; kind++) {
            for (unsigned si = 0u; si < sizeof seats; si++) {
                uint32_t t = make_sit(&g, (sit_t)sit);
                rc_action_t a = rc_t_act_seat((rc_action_kind_t)kind, seats[si]);
                if (is_valid(&g, a)) {
                    continue;
                }
                int fails = g_failures;
                memcpy(&before, &g, sizeof g);
                ASSERT_EQ(rc_game_apply(&g, a, t + 777u), RC_EVT_NONE);
                ASSERT_TRUE(memcmp(&before, &g, sizeof g) == 0);
                RC_CONTEXT_ON_FAIL(fails, "situation %d, action %d, seat %u",
                                   sit, kind, (unsigned)seats[si]);
                checked++;
            }
        }
    }
    ASSERT_TRUE(checked > 300u);
}

/* ---- the valid transitions, one by one ---- */

static void test_transitions(void) {
    static rc_game_t g, before;
    rc_settings_t defaults, kept;
    uint32_t t = 1000u;

    memset(&g, 0xA5, sizeof g);
    rc_game_init(&g);
    rc_settings_defaults(&defaults);
    ASSERT_EQ(g.state, RC_STATE_READY);
    ASSERT_EQ(g.return_state, RC_STATE_READY);
    ASSERT_EQ(g.active_seat, RC_NO_SEAT);
    ASSERT_EQ(g.viewed_seat, RC_NO_SEAT);
    ASSERT_EQ(g.game_seq, 0u);
    ASSERT_EQ(g.menu.row, 0u);
    ASSERT_TRUE(!g.alarm.fired_this_turn);
    ASSERT_TRUE(memcmp(&g.settings, &defaults, sizeof defaults) == 0);
    for (uint8_t i = 0u; i < RC_SEATS; i++) {
        ASSERT_EQ(g.players[i].seat, i);
        ASSERT_TRUE(!g.players[i].in_turn);
        ASSERT_EQ(g.players[i].log.count, 0u);
        ASSERT_EQ(g.players[i].turns_started, 0u);
    }
    ASSERT_EQ(rc_game_global_ms(&g, t + 50000u), 0u);
    RC_T_CHECK_INVARIANTS(&g, t);

    /* READY --START--> RUNNING, seat 0's turn. */
    ASSERT_EQ(rc_t_apply(&g, ACT(START), t), RC_EVT_NONE);
    ASSERT_EQ(g.state, RC_STATE_RUNNING);
    ASSERT_EQ(g.active_seat, 0u);
    ASSERT_TRUE(g.players[0].in_turn);
    ASSERT_EQ(g.players[0].turns_started, 1u);
    ASSERT_EQ(rc_game_turn_ms(&g, 0u, t), 0u);
    t += 3000u;
    ASSERT_EQ(rc_game_turn_ms(&g, 0u, t), 3000u);
    ASSERT_EQ(rc_game_total_ms(&g, 0u, t), 3000u);
    ASSERT_EQ(rc_game_global_ms(&g, t), 3000u);
    /* Out-of-range seats read 0. */
    ASSERT_EQ(rc_game_turn_ms(&g, 4u, t), 0u);
    ASSERT_EQ(rc_game_total_ms(&g, RC_NO_SEAT, t), 0u);

    /* SWITCH to the active seat changes nothing (G6, invariant 6). */
    memcpy(&before, &g, sizeof g);
    ASSERT_EQ(rc_t_apply(&g, ACTS(SWITCH, 0), t), RC_EVT_NONE);
    ASSERT_TRUE(memcmp(&before, &g, sizeof g) == 0);

    /* RUNNING --SWITCH 2--> RUNNING. */
    ASSERT_EQ(rc_t_apply(&g, ACTS(SWITCH, 2), t), RC_EVT_NONE);
    ASSERT_EQ(g.state, RC_STATE_RUNNING);
    ASSERT_EQ(g.active_seat, 2u);
    ASSERT_TRUE(!g.players[0].in_turn);
    ASSERT_EQ(g.players[0].log.count, 1u);
    ASSERT_EQ(g.players[0].log.turns_ms[0], 3000u);
    ASSERT_TRUE(g.players[2].in_turn);
    ASSERT_EQ(g.players[2].turns_started, 1u);
    ASSERT_EQ(rc_game_turn_ms(&g, 2u, t), 0u);
    ASSERT_EQ(rc_game_turn_ms(&g, 0u, t), 0u);
    t += 2000u;

    /* RUNNING --PAUSE--> PAUSED: frozen, turn not ended. */
    ASSERT_EQ(rc_t_apply(&g, ACT(PAUSE), t), RC_EVT_NONE);
    ASSERT_EQ(g.state, RC_STATE_PAUSED);
    ASSERT_TRUE(g.players[2].in_turn);
    ASSERT_TRUE(!g.players[2].turn.running);
    ASSERT_EQ(g.players[2].log.count, 0u);
    t += 50000u;
    ASSERT_EQ(rc_game_turn_ms(&g, 2u, t), 2000u);
    ASSERT_EQ(rc_game_global_ms(&g, t), 5000u);

    /* PAUSED --RESUME--> RUNNING: the same turn continues (G7). */
    ASSERT_EQ(rc_t_apply(&g, ACT(RESUME), t), RC_EVT_NONE);
    ASSERT_EQ(g.state, RC_STATE_RUNNING);
    t += 1000u;
    ASSERT_EQ(rc_game_turn_ms(&g, 2u, t), 3000u);
    ASSERT_EQ(g.players[2].turns_started, 1u);
    ASSERT_EQ(g.players[2].log.count, 0u);

    /* PAUSED --OPEN_SETTINGS--> SETTINGS (return PAUSED, row 0). */
    rc_t_apply(&g, ACT(PAUSE), t);
    g.menu.row = 6u;   /* stale cursor from an earlier visit */
    ASSERT_EQ(rc_t_apply(&g, ACT(OPEN_SETTINGS), t), RC_EVT_NONE);
    ASSERT_EQ(g.state, RC_STATE_SETTINGS);
    ASSERT_EQ(g.return_state, RC_STATE_PAUSED);
    ASSERT_EQ(g.menu.row, 0u);

    /* Cursor: UP/DOWN wrap over 9 rows. */
    rc_t_apply(&g, ACT(MENU_DOWN), t);
    ASSERT_EQ(g.menu.row, 1u);
    rc_t_apply(&g, ACT(MENU_UP), t);
    ASSERT_EQ(g.menu.row, 0u);
    rc_t_apply(&g, ACT(MENU_UP), t);
    ASSERT_EQ(g.menu.row, 8u);
    rc_t_apply(&g, ACT(MENU_DOWN), t);
    ASSERT_EQ(g.menu.row, 0u);

    /* Values: row 3 is P2's nickname. PREV/NEXT are -1/+1. */
    for (unsigned i = 0u; i < 3u; i++) {
        rc_t_apply(&g, ACT(MENU_DOWN), t);
    }
    ASSERT_EQ(g.menu.row, 3u);
    rc_t_apply(&g, ACT(MENU_NEXT), t);
    ASSERT_EQ(g.settings.seat[1].nickname_id, 2u);   /* Alex -> Ahmed */
    ASSERT_EQ(g.settings.seat[2].nickname_id, 1u);   /* swapped       */
    rc_t_apply(&g, ACT(MENU_PREV), t);
    ASSERT_EQ(g.settings.seat[1].nickname_id, 1u);
    ASSERT_EQ(g.settings.seat[2].nickname_id, 2u);
    rc_t_apply(&g, ACT(MENU_PREV), t);
    ASSERT_EQ(g.settings.seat[1].nickname_id, 0u);   /* Alex -> Jacob */
    ASSERT_EQ(g.settings.seat[0].nickname_id, 1u);

    /* Time does not run in SETTINGS. */
    ASSERT_EQ(rc_game_global_ms(&g, t), 6000u);
    t += 30000u;
    ASSERT_EQ(rc_game_global_ms(&g, t), 6000u);

    /* SETTINGS --MENU_DONE--> PAUSED. */
    ASSERT_EQ(rc_t_apply(&g, ACT(MENU_DONE), t), RC_EVT_NONE);
    ASSERT_EQ(g.state, RC_STATE_PAUSED);
    ASSERT_EQ(rc_game_turn_ms(&g, 2u, t), 3000u);

    /* Re-opening resets the cursor. */
    rc_t_apply(&g, ACT(OPEN_SETTINGS), t);
    ASSERT_EQ(g.menu.row, 0u);
    rc_t_apply(&g, ACT(MENU_DONE), t);
    ASSERT_EQ(g.state, RC_STATE_PAUSED);

    /* PAUSED --END_GAME--> OVER: the paused turn is ended and logged. */
    ASSERT_EQ(rc_t_apply(&g, ACT(END_GAME), t), RC_EVT_GAME_OVER);
    ASSERT_EQ(g.state, RC_STATE_OVER);
    ASSERT_EQ(g.viewed_seat, 2u);
    ASSERT_EQ(g.active_seat, 2u);
    ASSERT_TRUE(!g.players[2].in_turn);
    ASSERT_EQ(g.players[2].log.count, 1u);
    ASSERT_EQ(g.players[2].log.turns_ms[0], 3000u);
    ASSERT_EQ(rc_game_global_ms(&g, t), 6000u);
    ASSERT_EQ(rc_game_global_ms(&g, t + 1000000u), 6000u);   /* frozen */
    for (uint8_t i = 0u; i < RC_SEATS; i++) {
        ASSERT_EQ(rc_game_total_ms(&g, i, t),
                  rc_turnlog_sum_ms(&g.players[i].log));
    }

    /* OVER --VIEW s--> OVER. */
    ASSERT_EQ(rc_t_apply(&g, ACTS(VIEW, 1), t), RC_EVT_NONE);
    ASSERT_EQ(g.state, RC_STATE_OVER);
    ASSERT_EQ(g.viewed_seat, 1u);
    rc_t_apply(&g, ACTS(VIEW, 3), t);
    ASSERT_EQ(g.viewed_seat, 3u);

    /* OVER --NEW_GAME--> READY: clocks zero, settings kept, game_seq++. */
    memcpy(&kept, &g.settings, sizeof kept);
    ASSERT_TRUE(memcmp(&kept, &defaults, sizeof kept) != 0);
    ASSERT_EQ(rc_t_apply(&g, ACT(NEW_GAME), t), RC_EVT_NONE);
    ASSERT_EQ(g.state, RC_STATE_READY);
    ASSERT_EQ(g.game_seq, 1u);
    ASSERT_EQ(g.active_seat, RC_NO_SEAT);
    ASSERT_EQ(g.viewed_seat, RC_NO_SEAT);
    ASSERT_TRUE(!g.alarm.fired_this_turn);
    ASSERT_TRUE(memcmp(&g.settings, &kept, sizeof kept) == 0);
    for (uint8_t i = 0u; i < RC_SEATS; i++) {
        ASSERT_EQ(g.players[i].seat, i);
        ASSERT_TRUE(!g.players[i].in_turn);
        ASSERT_EQ(g.players[i].log.count, 0u);
        ASSERT_EQ(g.players[i].turns_started, 0u);
    }
    ASSERT_EQ(rc_game_global_ms(&g, t), 0u);

    /* READY --OPEN_SETTINGS--> SETTINGS --MENU_DONE--> READY. */
    rc_t_apply(&g, ACT(OPEN_SETTINGS), t);
    ASSERT_EQ(g.state, RC_STATE_SETTINGS);
    ASSERT_EQ(g.return_state, RC_STATE_READY);
    rc_t_apply(&g, ACT(MENU_DONE), t);
    ASSERT_EQ(g.state, RC_STATE_READY);

    /* And a second game starts like the first. */
    rc_t_apply(&g, ACT(START), t);
    ASSERT_EQ(g.state, RC_STATE_RUNNING);
    ASSERT_EQ(g.active_seat, 0u);
    ASSERT_EQ(g.game_seq, 1u);
}

/* ---- documents/02 acceptance script, AT-01..AT-13, on the model ---- */

static void test_acceptance_script(void) {
    static rc_game_t g, before;
    uint32_t t = 500u;

    rc_game_init(&g);

    /* AT-01: READY shows P1 with zero clocks. */
    ASSERT_EQ(rc_game_shown_seat(&g), 0u);
    ASSERT_EQ(rc_game_turn_ms(&g, 0u, t), 0u);
    ASSERT_EQ(rc_game_global_ms(&g, t), 0u);
    ASSERT_TRUE(!rc_game_turn_over_limit(&g, t));

    /* AT-02: yellow, red x4 to P2's faction, blue x4 to Lizard Cult. */
    rc_t_apply(&g, ACT(OPEN_SETTINGS), t);
    ASSERT_EQ(rc_game_shown_seat(&g), 0u);
    for (unsigned i = 0u; i < 4u; i++) {
        rc_t_apply(&g, ACT(MENU_DOWN), t);
    }
    ASSERT_EQ(g.menu.row, 4u);
    for (unsigned i = 0u; i < 4u; i++) {
        rc_t_apply(&g, ACT(MENU_NEXT), t);
    }
    ASSERT_EQ(g.settings.seat[1].faction_id, RC_FACTION_CULT);

    /* AT-03: gray x3 to P1's nickname, blue: P1 Alex, P2 Jacob. */
    for (unsigned i = 0u; i < 3u; i++) {
        rc_t_apply(&g, ACT(MENU_UP), t);
    }
    ASSERT_EQ(g.menu.row, 1u);
    rc_t_apply(&g, ACT(MENU_NEXT), t);
    ASSERT_EQ(g.settings.seat[0].nickname_id, 1u);
    ASSERT_EQ(g.settings.seat[1].nickname_id, 0u);

    /* AT-04: gray to LIMIT, blue to 0:30, green back to READY. */
    rc_t_apply(&g, ACT(MENU_UP), t);
    ASSERT_EQ(g.menu.row, 0u);
    rc_t_apply(&g, ACT(MENU_NEXT), t);
    ASSERT_EQ(rc_settings_limit_ms(&g.settings), 30000u);
    rc_t_apply(&g, ACT(MENU_DONE), t);
    ASSERT_EQ(g.state, RC_STATE_READY);

    /* AT-05: red starts P1; turn and GAME count together. */
    rc_t_apply(&g, ACT(START), t);
    ASSERT_EQ(g.state, RC_STATE_RUNNING);
    ASSERT_EQ(rc_t_advance(&g, &t, 10000u), 0u);
    ASSERT_EQ(rc_game_turn_ms(&g, 0u, t), 10000u);
    ASSERT_EQ(rc_game_global_ms(&g, t), 10000u);

    /* AT-06: P1's own button at 0:10 resets nothing. */
    memcpy(&before, &g, sizeof g);
    rc_t_apply(&g, ACTS(SWITCH, 0), t);
    ASSERT_TRUE(memcmp(&before, &g, sizeof g) == 0);
    ASSERT_EQ(rc_t_advance(&g, &t, 5000u), 0u);
    ASSERT_EQ(rc_game_turn_ms(&g, 0u, t), 15000u);

    /* AT-07: yellow at 0:15: P2 (now Jacob, Lizard Cult) from 00:00. */
    rc_t_apply(&g, ACTS(SWITCH, 1), t);
    ASSERT_EQ(rc_game_shown_seat(&g), 1u);
    ASSERT_EQ(rc_game_turn_ms(&g, 1u, t), 0u);
    ASSERT_EQ(g.settings.seat[1].nickname_id, 0u);
    ASSERT_EQ(g.settings.seat[1].faction_id, RC_FACTION_CULT);

    /* AT-08: one beep at 00:30, red from then on, still counting. */
    ASSERT_EQ(rc_t_advance(&g, &t, 29000u), 0u);
    ASSERT_TRUE(!rc_game_turn_over_limit(&g, t));
    ASSERT_EQ(rc_t_advance(&g, &t, 1000u), 1u);
    ASSERT_TRUE(rc_game_turn_over_limit(&g, t));
    ASSERT_EQ(rc_t_advance(&g, &t, 15000u), 0u);
    ASSERT_TRUE(rc_game_turn_over_limit(&g, t));
    ASSERT_EQ(rc_game_turn_ms(&g, 1u, t), 45000u);

    /* AT-09: gray: P1 at 00:00 in white, TOTAL ~0:15 and growing. */
    rc_t_apply(&g, ACTS(SWITCH, 0), t);
    ASSERT_TRUE(!rc_game_turn_over_limit(&g, t));
    ASSERT_EQ(rc_game_turn_ms(&g, 0u, t), 0u);
    ASSERT_EQ(rc_game_total_ms(&g, 0u, t), 15000u);
    ASSERT_EQ(rc_t_advance(&g, &t, 5000u), 0u);
    ASSERT_EQ(rc_game_total_ms(&g, 0u, t), 20000u);

    /* AT-10: red, 10 s frozen, red: the same turn continues. */
    rc_t_apply(&g, ACT(PAUSE), t);
    uint32_t frozen_global = rc_game_global_ms(&g, t);
    ASSERT_EQ(rc_t_advance(&g, &t, 10000u), 0u);
    ASSERT_EQ(rc_game_global_ms(&g, t), frozen_global);
    ASSERT_EQ(rc_game_turn_ms(&g, 0u, t), 5000u);
    ASSERT_EQ(rc_game_total_ms(&g, 0u, t), 20000u);
    rc_t_apply(&g, ACT(RESUME), t);
    ASSERT_EQ(rc_t_advance(&g, &t, 1000u), 0u);
    ASSERT_EQ(rc_game_turn_ms(&g, 0u, t), 6000u);
    ASSERT_EQ(g.players[0].turns_started, 2u);

    /* AT-11: green (P3), a few seconds, red, blue held: GAME OVER. */
    rc_t_apply(&g, ACTS(SWITCH, 2), t);
    ASSERT_EQ(rc_t_advance(&g, &t, 4000u), 0u);
    rc_t_apply(&g, ACT(PAUSE), t);
    t += 2000u;   /* the blue hold */
    ASSERT_EQ(rc_t_apply(&g, ACT(END_GAME), t), RC_EVT_GAME_OVER);
    ASSERT_EQ(g.state, RC_STATE_OVER);
    ASSERT_EQ(g.players[0].log.count, 2u);
    ASSERT_EQ(g.players[1].log.count, 1u);
    ASSERT_EQ(g.players[2].log.count, 1u);
    ASSERT_EQ(g.players[3].log.count, 0u);
    ASSERT_EQ(rc_game_total_ms(&g, 0u, t), 21000u);
    ASSERT_EQ(rc_game_total_ms(&g, 1u, t), 45000u);
    ASSERT_EQ(rc_game_total_ms(&g, 2u, t), 4000u);
    ASSERT_EQ(rc_game_global_ms(&g, t), 70000u);

    /* AT-12: each player's final card; P4 never played. */
    for (uint8_t s = 0u; s < RC_SEATS; s++) {
        rc_t_apply(&g, ACTS(VIEW, s), t);
        ASSERT_EQ(rc_game_shown_seat(&g), s);
    }
    ASSERT_EQ(rc_game_total_ms(&g, 3u, t), 0u);

    /* AT-13: blue held: READY, zero clocks, AT-02..04 settings kept. */
    t += 2000u;
    rc_t_apply(&g, ACT(NEW_GAME), t);
    ASSERT_EQ(g.state, RC_STATE_READY);
    ASSERT_EQ(rc_game_global_ms(&g, t), 0u);
    ASSERT_EQ(rc_game_shown_seat(&g), 0u);
    ASSERT_EQ(g.settings.seat[1].faction_id, RC_FACTION_CULT);
    ASSERT_EQ(g.settings.seat[0].nickname_id, 1u);
    ASSERT_EQ(g.settings.seat[1].nickname_id, 0u);
    ASSERT_EQ(rc_settings_limit_ms(&g.settings), 30000u);
}

/* ---- TurnLimitAlarm inside the game ---- */

static void test_alarm_in_game(void) {
    static rc_game_t g;
    uint32_t t = 0xFFFFF000u;   /* the first turn straddles the uint32 wrap */

    rc_game_init(&g);
    set_limit(&g, 2u, t);       /* 1:00 */
    ASSERT_EQ(rc_settings_limit_ms(&g.settings), 60000u);

    /* One beep on the crossing, none after; red stays. */
    rc_t_apply(&g, ACT(START), t);
    ASSERT_EQ(rc_t_advance(&g, &t, 59000u), 0u);
    ASSERT_TRUE(!rc_game_turn_over_limit(&g, t));
    ASSERT_EQ(rc_t_advance(&g, &t, 1000u), 1u);
    ASSERT_TRUE(rc_game_turn_over_limit(&g, t));
    ASSERT_EQ(rc_t_advance(&g, &t, 120000u), 0u);
    ASSERT_TRUE(rc_game_turn_over_limit(&g, t));

    /* The next turn starts white and beeps again on its own crossing. */
    rc_t_apply(&g, ACTS(SWITCH, 1), t);
    ASSERT_TRUE(!rc_game_turn_over_limit(&g, t));
    ASSERT_TRUE(!g.alarm.fired_this_turn);
    ASSERT_EQ(rc_t_advance(&g, &t, 60000u), 1u);

    /* Paused: still red, no beeps. */
    rc_t_apply(&g, ACT(PAUSE), t);
    ASSERT_TRUE(rc_game_turn_over_limit(&g, t));
    ASSERT_EQ(rc_t_advance(&g, &t, 5000u), 0u);

    /* G13: lower the limit under a paused turn already past it: red at
     * once, and no beep on resume. */
    rc_t_apply(&g, ACT(RESUME), t);
    rc_t_apply(&g, ACTS(SWITCH, 2), t);
    ASSERT_EQ(rc_t_advance(&g, &t, 45000u), 0u);
    rc_t_apply(&g, ACT(PAUSE), t);
    ASSERT_TRUE(!rc_game_turn_over_limit(&g, t));
    rc_t_apply(&g, ACT(OPEN_SETTINGS), t);
    rc_t_apply(&g, ACT(MENU_PREV), t);            /* 1:00 -> 0:30 */
    ASSERT_EQ(rc_settings_limit_ms(&g.settings), 30000u);
    rc_t_apply(&g, ACT(MENU_DONE), t);
    ASSERT_EQ(g.state, RC_STATE_PAUSED);
    ASSERT_TRUE(rc_game_turn_over_limit(&g, t));
    ASSERT_TRUE(g.alarm.fired_this_turn);
    rc_t_apply(&g, ACT(RESUME), t);
    ASSERT_EQ(rc_t_advance(&g, &t, 10000u), 0u);  /* silent red */
    ASSERT_TRUE(rc_game_turn_over_limit(&g, t));

    /* Raising the limit back above the turn re-arms it: rc_alarm_sync is
     * defined as fired = over(turn, new limit), so this turn now beeps when
     * it crosses the NEW limit. (A deliberate settings edit, not a repeat
     * of the old crossing.) */
    rc_t_apply(&g, ACT(PAUSE), t);                 /* turn at 0:55 */
    rc_t_apply(&g, ACT(OPEN_SETTINGS), t);
    rc_t_apply(&g, ACT(MENU_NEXT), t);             /* 0:30 -> 1:00 */
    rc_t_apply(&g, ACT(MENU_DONE), t);
    ASSERT_TRUE(!rc_game_turn_over_limit(&g, t));
    ASSERT_TRUE(!g.alarm.fired_this_turn);
    rc_t_apply(&g, ACT(RESUME), t);
    ASSERT_EQ(rc_t_advance(&g, &t, 4000u), 0u);
    ASSERT_EQ(rc_t_advance(&g, &t, 1000u), 1u);    /* turn hits 1:00 */
    ASSERT_EQ(rc_t_advance(&g, &t, 30000u), 0u);

    /* Only while RUNNING: an armed alarm on a turn already past the limit
     * stays silent while paused and fires on the first RUNNING tick. (Limit
     * poked behind the menu's back so nothing syncs.) */
    rc_t_apply(&g, ACT(PAUSE), t);
    g.settings.limit_idx = 1u;                /* 0:30 */
    g.alarm.fired_this_turn = false;
    ASSERT_EQ(rc_game_tick(&g, t), RC_EVT_NONE);
    ASSERT_EQ(rc_game_tick(&g, t + 5000u), RC_EVT_NONE);
    rc_t_apply(&g, ACT(RESUME), t);
    ASSERT_EQ(rc_game_tick(&g, t), RC_EVT_BEEP);
    ASSERT_EQ(rc_game_tick(&g, t), RC_EVT_NONE);

    /* Limit Off: never red, never a beep. */
    rc_t_apply(&g, ACT(PAUSE), t);
    rc_t_apply(&g, ACT(OPEN_SETTINGS), t);
    rc_t_apply(&g, ACT(MENU_PREV), t);             /* 0:30 -> Off */
    ASSERT_EQ(rc_settings_limit_ms(&g.settings), 0u);
    rc_t_apply(&g, ACT(MENU_DONE), t);
    ASSERT_TRUE(!rc_game_turn_over_limit(&g, t));
    rc_t_apply(&g, ACT(RESUME), t);
    ASSERT_EQ(rc_t_advance(&g, &t, 900000u), 0u);
    ASSERT_TRUE(!rc_game_turn_over_limit(&g, t));

    /* No tick outside RUNNING ever beeps. */
    g.settings.limit_idx = 1u;
    rc_t_apply(&g, ACT(PAUSE), t);
    ASSERT_EQ(rc_game_tick(&g, t), RC_EVT_NONE);
    rc_t_apply(&g, ACT(OPEN_SETTINGS), t);
    ASSERT_EQ(rc_game_tick(&g, t), RC_EVT_NONE);
    rc_t_apply(&g, ACT(MENU_DONE), t);
    ASSERT_EQ(rc_t_apply(&g, ACT(END_GAME), t), RC_EVT_GAME_OVER);
    ASSERT_EQ(rc_game_tick(&g, t), RC_EVT_NONE);
    ASSERT_TRUE(!rc_game_turn_over_limit(&g, t));   /* OVER: never red */
    rc_t_apply(&g, ACT(NEW_GAME), t);
    ASSERT_EQ(rc_game_tick(&g, t), RC_EVT_NONE);
    ASSERT_TRUE(!rc_game_turn_over_limit(&g, t));   /* READY: never red */
}

/* ---- what S1 shows ---- */

static void test_shown_seat_and_red(void) {
    static rc_game_t g;
    uint32_t t = 100u;

    rc_game_init(&g);
    ASSERT_EQ(rc_game_shown_seat(&g), 0u);
    rc_t_apply(&g, ACT(OPEN_SETTINGS), t);
    ASSERT_EQ(rc_game_shown_seat(&g), 0u);   /* would show READY's P1 */
    rc_t_apply(&g, ACT(MENU_NEXT), t);            /* limit 0:30 */
    rc_t_apply(&g, ACT(MENU_DONE), t);

    rc_t_apply(&g, ACT(START), t);
    ASSERT_EQ(rc_game_shown_seat(&g), 0u);
    rc_t_apply(&g, ACTS(SWITCH, 3), t);
    ASSERT_EQ(rc_game_shown_seat(&g), 3u);
    rc_t_advance(&g, &t, 40000u);
    ASSERT_TRUE(rc_game_turn_over_limit(&g, t));
    rc_t_apply(&g, ACT(PAUSE), t);
    ASSERT_EQ(rc_game_shown_seat(&g), 3u);
    ASSERT_TRUE(rc_game_turn_over_limit(&g, t));     /* paused, still past */
    rc_t_apply(&g, ACT(OPEN_SETTINGS), t);
    ASSERT_EQ(rc_game_shown_seat(&g), 3u);           /* would show PAUSED's */
    rc_t_apply(&g, ACT(MENU_DONE), t);
    rc_t_apply(&g, ACT(END_GAME), t);
    ASSERT_EQ(rc_game_shown_seat(&g), 3u);           /* last active */
    ASSERT_TRUE(!rc_game_turn_over_limit(&g, t));    /* OVER: never red */
    rc_t_apply(&g, ACTS(VIEW, 1), t);
    ASSERT_EQ(rc_game_shown_seat(&g), 1u);
    rc_t_apply(&g, ACT(NEW_GAME), t);
    ASSERT_EQ(rc_game_shown_seat(&g), 0u);
}

/* ---- a long random game through the InputMapper ---- */

static uint32_t lcg_next(uint32_t *s) {
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

static void test_random_play(void) {
    static rc_game_t g, before;
    rc_input_t in;
    uint32_t rng = 0xC0FFEEu;
    uint32_t now = 0xFFF00000u;   /* wraps about 17 minutes in */
    uint8_t down = 0u;
    uint32_t prev_global = 0u;
    uint32_t key_seq = 0xFFFFFFFFu;   /* identifies the current turn */
    uint8_t key_seat = RC_NO_SEAT;
    uint16_t key_turn = 0u;
    unsigned beeps_this_turn = 0u;
    unsigned states_seen = 0u, games_over = 0u, new_games = 0u, beeps = 0u;
    unsigned truncations = 0u;

    rc_game_init(&g);
    rc_input_init(&in);
    for (unsigned step = 0u; step < 100000u; step++) {
        int fails = g_failures;

        /* Time passes: mostly a loop-ish interval, sometimes a long think. */
        uint32_t r = lcg_next(&rng);
        now += ((r & 7u) == 0u) ? (lcg_next(&rng) % 90000u)
                                : (lcg_next(&rng) % 1500u);

        /* Buttons: toggle one at random half the time, now and then two. */
        uint8_t pressed = 0u, released = 0u;
        r = lcg_next(&rng);
        unsigned toggles = ((r & 1u) == 0u) ? 0u : (((r >> 1) & 7u) == 0u ? 2u : 1u);
        for (unsigned k = 0u; k < toggles; k++) {
            uint8_t bit = RC_BTN_BIT(lcg_next(&rng) % RC_BTN_COUNT);
            if ((down & bit) != 0u) {
                released |= bit;
                down = (uint8_t)(down & ~bit);
            } else {
                pressed |= bit;
                down |= bit;
            }
        }
        rc_buttons_t b = rc_t_btn(down, pressed, released);

        rc_state_t st0 = g.state;
        rc_action_t a = rc_input_map(&in, g.state, b, now);
        ASSERT_TRUE(rc_input_hold_progress(&in, g.state, now) <= 100u);

        memcpy(&before, &g, sizeof g);
        rc_events_t ev = rc_game_apply(&g, a, now);

        /* 6: SWITCH to the active seat changes nothing. */
        if (st0 == RC_STATE_RUNNING && a.kind == RC_ACT_SWITCH &&
            a.seat == before.active_seat) {
            ASSERT_TRUE(memcmp(&before, &g, sizeof g) == 0);
        }
        if ((ev & RC_EVT_GAME_OVER) != 0u) {
            games_over++;
            ASSERT_EQ(st0, RC_STATE_PAUSED);
            ASSERT_EQ(g.state, RC_STATE_OVER);
            for (uint8_t i = 0u; i < RC_SEATS; i++) {
                ASSERT_EQ(rc_game_total_ms(&g, i, now),
                          rc_turnlog_sum_ms(&g.players[i].log));
                truncations += g.players[i].log.truncated ? 1u : 0u;
            }
        }
        bool fresh = (st0 == RC_STATE_OVER && g.state == RC_STATE_READY);
        if (fresh) {
            new_games++;
        }

        ev |= rc_game_tick(&g, now);

        /* 5: at most one beep per turn, only while RUNNING and over the
         * limit. A settings visit may legitimately re-arm (rc_alarm_sync). */
        uint16_t ts = (g.active_seat < RC_SEATS)
                          ? g.players[g.active_seat].turns_started : 0u;
        if (g.game_seq != key_seq || g.active_seat != key_seat ||
            ts != key_turn ||
            (st0 == RC_STATE_SETTINGS && g.state != RC_STATE_SETTINGS)) {
            key_seq = g.game_seq;
            key_seat = g.active_seat;
            key_turn = ts;
            beeps_this_turn = 0u;
        }
        if ((ev & RC_EVT_BEEP) != 0u) {
            beeps++;
            beeps_this_turn++;
            ASSERT_EQ(g.state, RC_STATE_RUNNING);
            ASSERT_TRUE(rc_game_turn_over_limit(&g, now));
            ASSERT_TRUE(beeps_this_turn <= 1u);
        }

        /* 1-4, plus: what S1 shows is always a real seat, and time never
         * runs backwards within a game. */
        RC_T_CHECK_INVARIANTS(&g, now);
        ASSERT_TRUE(rc_game_shown_seat(&g) < RC_SEATS);
        uint32_t global = rc_game_global_ms(&g, now);
        if (!fresh) {
            ASSERT_TRUE(global >= prev_global);
        }
        prev_global = global;
        states_seen |= 1u << (unsigned)g.state;

        if (g_failures != fails) {
            printf("  context: random play step %u, state %d -> %d, action %d "
                   "seat %u, buttons d%02X p%02X r%02X\n", step, (int)st0,
                   (int)g.state, (int)a.kind, (unsigned)a.seat,
                   (unsigned)b.down, (unsigned)b.pressed, (unsigned)b.released);
            break;
        }
    }

    /* The walk must actually have visited everything it claims to test. */
    ASSERT_EQ(states_seen, 0x1Fu);
    ASSERT_TRUE(games_over >= 10u);
    ASSERT_TRUE(new_games >= 10u);
    ASSERT_TRUE(beeps >= 10u);
    printf("random play: %u games over, %u new games, %u beeps, "
           "%u truncated logs\n", games_over, new_games, beeps, truncations);
}

int main(void) {
    test_ignored_actions();
    test_transitions();
    test_acceptance_script();
    test_alarm_in_game();
    test_shown_seat_and_red();
    test_random_play();
    TEST_RETURN();
}
