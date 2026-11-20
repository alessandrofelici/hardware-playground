/* rootclock host tests -- Stopwatch, TurnLog, Player. */
#include "rc_test_support.h"

#include "rc_player.h"
#include "rc_stopwatch.h"

static void test_stopwatch_basic(void) {
    rc_stopwatch_t sw;
    memset(&sw, 0xA5, sizeof sw);
    rc_stopwatch_reset(&sw);
    ASSERT_TRUE(!sw.running);
    ASSERT_EQ(rc_stopwatch_elapsed_ms(&sw, 12345u), 0u);

    rc_stopwatch_start(&sw, 1000u);
    ASSERT_TRUE(sw.running);
    ASSERT_EQ(rc_stopwatch_elapsed_ms(&sw, 1000u), 0u);
    ASSERT_EQ(rc_stopwatch_elapsed_ms(&sw, 1500u), 500u);

    /* start while running is a no-op: the interval keeps its origin. */
    rc_stopwatch_start(&sw, 1400u);
    ASSERT_EQ(rc_stopwatch_elapsed_ms(&sw, 1500u), 500u);

    rc_stopwatch_stop(&sw, 2000u);
    ASSERT_TRUE(!sw.running);
    ASSERT_EQ(rc_stopwatch_elapsed_ms(&sw, 99999u), 1000u);  /* frozen */

    /* stop while stopped is a no-op. */
    rc_stopwatch_stop(&sw, 5000u);
    ASSERT_EQ(rc_stopwatch_elapsed_ms(&sw, 5000u), 1000u);

    /* The stopped span 2000..10000 is not counted. */
    rc_stopwatch_start(&sw, 10000u);
    ASSERT_EQ(rc_stopwatch_elapsed_ms(&sw, 10250u), 1250u);
    rc_stopwatch_stop(&sw, 10250u);
    ASSERT_EQ(sw.accumulated_ms, 1250u);

    /* Many short pauses add up exactly. */
    uint32_t t = 20000u;
    for (unsigned i = 0u; i < 100u; i++) {
        rc_stopwatch_start(&sw, t);
        t += 7u;
        rc_stopwatch_stop(&sw, t);
        t += 1000u;
    }
    ASSERT_EQ(rc_stopwatch_elapsed_ms(&sw, t), 1250u + 700u);

    rc_stopwatch_reset(&sw);
    ASSERT_TRUE(!sw.running);
    ASSERT_EQ(rc_stopwatch_elapsed_ms(&sw, t), 0u);
}

static void test_stopwatch_wrap(void) {
    rc_stopwatch_t sw;

    /* One run interval straddling 2^32 ms. */
    rc_stopwatch_reset(&sw);
    rc_stopwatch_start(&sw, 0xFFFFFF00u);
    ASSERT_EQ(rc_stopwatch_elapsed_ms(&sw, 0xFFFFFFFFu), 0xFFu);
    ASSERT_EQ(rc_stopwatch_elapsed_ms(&sw, 0x00000000u), 0x100u);
    ASSERT_EQ(rc_stopwatch_elapsed_ms(&sw, 0x00000100u), 0x200u);
    rc_stopwatch_stop(&sw, 0x00000100u);
    ASSERT_EQ(sw.accumulated_ms, 0x200u);

    /* Run, pause across the wrap, run again: the paused span is excluded. */
    rc_stopwatch_reset(&sw);
    rc_stopwatch_start(&sw, 0xFFFFF000u);
    rc_stopwatch_stop(&sw, 0xFFFFF800u);            /* 2048 run          */
    rc_stopwatch_start(&sw, 0x00000400u);           /* 3072 paused, wrap */
    ASSERT_EQ(rc_stopwatch_elapsed_ms(&sw, 0x00000C00u), 4096u);
}

static void test_turnlog_overflow(void) {
    static rc_turnlog_t log;
    uint32_t expect_sum = 0u;

    memset(&log, 0xA5, sizeof log);
    rc_turnlog_clear(&log);
    ASSERT_EQ(log.count, 0u);
    ASSERT_TRUE(!log.truncated);
    ASSERT_EQ(rc_turnlog_sum_ms(&log), 0u);

    for (uint32_t i = 0u; i < RC_MAX_TURNS; i++) {
        uint32_t ms = i * 1000u + 7u;
        rc_turnlog_append(&log, ms);
        expect_sum += ms;
    }
    /* Exactly full is not truncated. */
    ASSERT_EQ(log.count, RC_MAX_TURNS);
    ASSERT_TRUE(!log.truncated);
    ASSERT_EQ(rc_turnlog_sum_ms(&log), expect_sum);
    ASSERT_EQ(log.turns_ms[0], 7u);
    ASSERT_EQ(log.turns_ms[RC_MAX_TURNS - 1u], 127007u);

    /* Turn 129 folds into the last slot. */
    rc_turnlog_append(&log, 500u);
    expect_sum += 500u;
    ASSERT_EQ(log.count, RC_MAX_TURNS);
    ASSERT_TRUE(log.truncated);
    ASSERT_EQ(log.turns_ms[RC_MAX_TURNS - 1u], 127507u);
    ASSERT_EQ(rc_turnlog_sum_ms(&log), expect_sum);

    /* And so do 130..200; earlier slots never move. */
    for (uint32_t i = 0u; i < 72u; i++) {
        rc_turnlog_append(&log, 1500u);
        expect_sum += 1500u;
    }
    ASSERT_EQ(log.count, RC_MAX_TURNS);
    ASSERT_TRUE(log.truncated);
    ASSERT_EQ(log.turns_ms[RC_MAX_TURNS - 1u], 127507u + 72u * 1500u);
    ASSERT_EQ(log.turns_ms[0], 7u);
    ASSERT_EQ(log.turns_ms[RC_MAX_TURNS - 2u], 126007u);
    ASSERT_EQ(rc_turnlog_sum_ms(&log), expect_sum);

    rc_turnlog_clear(&log);
    ASSERT_EQ(log.count, 0u);
    ASSERT_TRUE(!log.truncated);
    ASSERT_EQ(rc_turnlog_sum_ms(&log), 0u);
}

static void test_player_turns(void) {
    rc_player_t p;
    memset(&p, 0xA5, sizeof p);
    rc_player_init(&p, 2u);
    ASSERT_EQ(p.seat, 2u);
    ASSERT_TRUE(!p.in_turn);
    ASSERT_TRUE(!p.turn.running);
    ASSERT_EQ(p.turns_started, 0u);
    ASSERT_EQ(p.log.count, 0u);
    ASSERT_EQ(rc_player_turn_ms(&p, 5000u), 0u);
    ASSERT_EQ(rc_player_total_ms(&p, 5000u), 0u);

    /* Not in a turn: end/pause/resume are no-ops. */
    rc_player_end_turn(&p, 100u);
    rc_player_pause(&p, 100u);
    rc_player_resume(&p, 100u);
    ASSERT_TRUE(!p.in_turn);
    ASSERT_TRUE(!p.turn.running);
    ASSERT_EQ(p.log.count, 0u);
    ASSERT_EQ(rc_player_total_ms(&p, 9000u), 0u);

    rc_player_begin_turn(&p, 1000u);
    ASSERT_TRUE(p.in_turn);
    ASSERT_TRUE(p.turn.running);
    ASSERT_EQ(p.turns_started, 1u);
    ASSERT_EQ(rc_player_turn_ms(&p, 1000u), 0u);
    ASSERT_EQ(rc_player_turn_ms(&p, 4000u), 3000u);
    ASSERT_EQ(rc_player_total_ms(&p, 4000u), 3000u);

    rc_player_pause(&p, 4000u);
    ASSERT_TRUE(p.in_turn);
    ASSERT_TRUE(!p.turn.running);
    ASSERT_EQ(rc_player_turn_ms(&p, 9000u), 3000u);    /* frozen */
    ASSERT_EQ(rc_player_total_ms(&p, 9000u), 3000u);
    rc_player_pause(&p, 9000u);                         /* no-op */
    rc_player_resume(&p, 9000u);
    ASSERT_EQ(rc_player_turn_ms(&p, 10000u), 4000u);   /* same turn */
    ASSERT_EQ(p.turns_started, 1u);
    ASSERT_EQ(p.log.count, 0u);

    rc_player_end_turn(&p, 10000u);
    ASSERT_TRUE(!p.in_turn);
    ASSERT_TRUE(!p.turn.running);
    ASSERT_EQ(p.log.count, 1u);
    ASSERT_EQ(p.log.turns_ms[0], 4000u);               /* pause excluded */
    ASSERT_EQ(rc_player_turn_ms(&p, 50000u), 0u);
    ASSERT_EQ(rc_player_total_ms(&p, 50000u), 4000u);

    rc_player_begin_turn(&p, 20000u);
    ASSERT_EQ(p.turns_started, 2u);
    ASSERT_EQ(rc_player_turn_ms(&p, 20500u), 500u);
    ASSERT_EQ(rc_player_total_ms(&p, 20500u), 4500u);

    /* begin_turn while in a turn ends that turn first. */
    rc_player_begin_turn(&p, 21000u);
    ASSERT_EQ(p.turns_started, 3u);
    ASSERT_EQ(p.log.count, 2u);
    ASSERT_EQ(p.log.turns_ms[1], 1000u);
    ASSERT_EQ(rc_player_turn_ms(&p, 21000u), 0u);

    rc_player_end_turn(&p, 22000u);
    ASSERT_EQ(p.log.count, 3u);
    ASSERT_EQ(p.log.turns_ms[2], 1000u);
    ASSERT_EQ(rc_player_total_ms(&p, 22000u), 6000u);
    ASSERT_EQ(p.seat, 2u);
}

/* 130 turns, starting just before the uint32 wrap: the log folds, and
 * total == sum(log) + turn holds at every point. */
static void test_player_overflow_invariant(void) {
    rc_player_t p;
    uint32_t now = 0xFFFF0000u;
    uint32_t expect = 0u;
    uint32_t tail = 0u;

    rc_player_init(&p, 0u);
    for (uint32_t i = 0u; i < 130u; i++) {
        uint32_t d = 1000u + i * 37u;
        rc_player_begin_turn(&p, now);
        now += d / 2u;
        ASSERT_EQ(rc_player_total_ms(&p, now), expect + d / 2u);
        rc_player_pause(&p, now);
        now += 60000u;                                  /* paused minute */
        rc_player_resume(&p, now);
        now += d - d / 2u;
        ASSERT_EQ(rc_player_total_ms(&p, now), expect + d);
        ASSERT_EQ(rc_player_total_ms(&p, now),
                  rc_turnlog_sum_ms(&p.log) + rc_player_turn_ms(&p, now));
        rc_player_end_turn(&p, now);
        expect += d;
        if (i >= RC_MAX_TURNS - 1u) {
            tail += d;
        }
    }
    ASSERT_EQ(p.turns_started, 130u);
    ASSERT_EQ(p.log.count, RC_MAX_TURNS);
    ASSERT_TRUE(p.log.truncated);
    ASSERT_EQ(p.log.turns_ms[RC_MAX_TURNS - 1u], tail);
    ASSERT_EQ(rc_turnlog_sum_ms(&p.log), expect);
    ASSERT_EQ(rc_player_total_ms(&p, now + 99999u), expect);
}

int main(void) {
    test_stopwatch_basic();
    test_stopwatch_wrap();
    test_turnlog_overflow();
    test_player_turns();
    test_player_overflow_invariant();
    TEST_RETURN();
}
