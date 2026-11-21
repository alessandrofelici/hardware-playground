/* rootclock host tests -- helpers shared by the test_rc_*.c files.
 *
 * Everything is static so each test binary gets its own copy; the failure
 * counter itself is the BSP's single extern g_failures (test_util.h). */
#ifndef RC_TEST_SUPPORT_H
#define RC_TEST_SUPPORT_H

#include <stdio.h>
#include <string.h>

#include "test_util.h"

#include "rc_game.h"
#include "rc_input.h"
#include "rc_types.h"

#define ASSERT_STREQ(actual, expected) do { \
    const char *_sa = (actual); \
    const char *_se = (expected); \
    if (_sa == NULL || strcmp(_sa, _se) != 0) { g_failures++; \
        printf("FAIL %s:%d: %s ==\n  \"%s\"\n expected\n  \"%s\"\n", \
               __FILE__, __LINE__, #actual, _sa ? _sa : "(null)", _se); } \
} while (0)

/* Print `what` once if any assertion failed since `before` was sampled, so
 * a failure inside a table-driven loop says which row it was. */
#define RC_CONTEXT_ON_FAIL(before, ...) do { \
    if (g_failures != (before)) { printf("  context: "); \
        printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static inline rc_action_t rc_t_act(rc_action_kind_t kind) {
    rc_action_t a = { kind, RC_NO_SEAT };
    return a;
}

static inline rc_action_t rc_t_act_seat(rc_action_kind_t kind, uint8_t seat) {
    rc_action_t a = { kind, seat };
    return a;
}

static inline rc_buttons_t rc_t_btn(uint8_t down, uint8_t pressed,
                                    uint8_t released) {
    rc_buttons_t b = { down, pressed, released };
    return b;
}

/* Number of turn stopwatches currently running. */
static inline unsigned rc_t_running_count(const rc_game_t *g) {
    unsigned n = 0u;
    for (unsigned i = 0u; i < RC_SEATS; i++) {
        if (g->players[i].turn.running) {
            n++;
        }
    }
    return n;
}

/* documents/03 invariants 1-4, plus the structural facts they rest on.
 * (5 and 6 need the events and the action, so callers check those.) */
static inline void rc_t_check_invariants_at(const rc_game_t *g, uint32_t now,
                                            const char *file, int line) {
    int before = g_failures;
    uint32_t sum_totals = 0u;
    unsigned in_turn = 0u;
    uint8_t seen = 0u;

    for (uint8_t i = 0u; i < RC_SEATS; i++) {
        const rc_player_t *p = &g->players[i];
        uint32_t total = rc_game_total_ms(g, i, now);
        sum_totals += total;
        /* 2: total == sum(turns) + current turn, truncated or not. */
        ASSERT_EQ(total, rc_turnlog_sum_ms(&p->log) + rc_player_turn_ms(p, now));
        ASSERT_TRUE(p->log.count <= RC_MAX_TURNS);
        ASSERT_TRUE(!p->turn.running || p->in_turn);
        if (p->in_turn) {
            in_turn++;
            ASSERT_EQ(i, g->active_seat);
        }
        /* 4: nickname ids are a permutation of 0..3. */
        ASSERT_TRUE(g->settings.seat[i].nickname_id < RC_NICKNAME_COUNT);
        seen |= (uint8_t)(1u << (g->settings.seat[i].nickname_id & 7u));
        ASSERT_TRUE(g->settings.seat[i].faction_id < RC_FACTION_COUNT);
    }
    ASSERT_EQ(seen, 0x0Fu);
    ASSERT_TRUE(g->settings.limit_idx < 11u);
    ASSERT_TRUE(in_turn <= 1u);

    /* 1: global == sum of totals. */
    ASSERT_EQ(rc_game_global_ms(g, now), sum_totals);

    /* 3: exactly one stopwatch runs in RUNNING (the active seat's), none
     *    otherwise. */
    if (g->state == RC_STATE_RUNNING) {
        ASSERT_EQ(rc_t_running_count(g), 1u);
        ASSERT_TRUE(g->active_seat < RC_SEATS);
        if (g->active_seat < RC_SEATS) {
            ASSERT_TRUE(g->players[g->active_seat].turn.running);
            ASSERT_TRUE(g->players[g->active_seat].in_turn);
        }
    } else {
        ASSERT_EQ(rc_t_running_count(g), 0u);
    }
    if (g->state == RC_STATE_READY || g->state == RC_STATE_OVER) {
        ASSERT_EQ(in_turn, 0u);
    }
    if (g->state == RC_STATE_PAUSED) {
        ASSERT_EQ(in_turn, 1u);
    }
    if (g->state == RC_STATE_SETTINGS) {
        ASSERT_TRUE(g->return_state == RC_STATE_READY ||
                    g->return_state == RC_STATE_PAUSED);
    }
    RC_CONTEXT_ON_FAIL(before, "invariants checked at %s:%d, state %d, now %lu",
                       file, line, (int)g->state, (unsigned long)now);
}

#define RC_T_CHECK_INVARIANTS(g, now) \
    rc_t_check_invariants_at((g), (now), __FILE__, __LINE__)

/* rc_game_apply, then the invariants. */
static inline rc_events_t rc_t_apply(rc_game_t *g, rc_action_t a,
                                     uint32_t now) {
    rc_events_t ev = rc_game_apply(g, a, now);
    RC_T_CHECK_INVARIANTS(g, now);
    return ev;
}

/* Let `ms` pass as the display loop would: steps of at most 1 s, a tick and
 * an invariant check after each. Returns the number of BEEP events. */
static inline unsigned rc_t_advance(rc_game_t *g, uint32_t *now, uint32_t ms) {
    unsigned beeps = 0u;
    while (ms > 0u) {
        uint32_t step = (ms < 1000u) ? ms : 1000u;
        *now += step;
        ms -= step;
        rc_events_t ev = rc_game_tick(g, *now);
        ASSERT_EQ(ev & ~RC_EVT_BEEP, 0u);
        if ((ev & RC_EVT_BEEP) != 0u) {
            beeps++;
            ASSERT_EQ(g->state, RC_STATE_RUNNING);   /* invariant 5 */
        }
        RC_T_CHECK_INVARIANTS(g, *now);
    }
    return beeps;
}

#define ACT(k)     rc_t_act(RC_ACT_##k)
#define ACTS(k, s) rc_t_act_seat(RC_ACT_##k, (uint8_t)(s))

#endif
