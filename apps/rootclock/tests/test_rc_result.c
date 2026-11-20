/* rootclock host tests -- GameResult and the contract v1 JSON (documents/04
 * section 2). The JSON is compared byte for byte. */
#include "rc_test_support.h"

#include "rc_result.h"
#include "rc_settings.h"

/* documents/04 section 2, "Example: a 3-player game with a 2:00 limit",
 * compacted exactly as the firmware must emit it. */
static const char k_doc_example[] =
    "{\"schema\":\"rootclock.game_result\",\"schema_version\":1,"
    "\"game_id\":\"7f3a9c21e04b5d18\","
    "\"device\":{\"app\":\"rootclock\",\"app_version\":\"002\"},"
    "\"ended_at\":null,\"turn_limit_ms\":120000,\"global_ms\":666000,"
    "\"players\":["
    "{\"seat\":1,\"nickname\":\"Jacob\",\"faction\":\"marquise\","
    "\"total_ms\":228000,\"turns_ms\":[62000,95000,71000],"
    "\"turns_truncated\":false},"
    "{\"seat\":2,\"nickname\":\"Alex\",\"faction\":\"eyrie\","
    "\"total_ms\":342000,\"turns_ms\":[88000,120000,134000],"
    "\"turns_truncated\":false},"
    "{\"seat\":3,\"nickname\":\"Ahmed\",\"faction\":\"woodland\","
    "\"total_ms\":96000,\"turns_ms\":[45000,51000],"
    "\"turns_truncated\":false}"
    "]}";

static char g_json[RC_RESULT_JSON_MAX];

static unsigned count_substr(const char *hay, const char *needle) {
    unsigned n = 0u;
    size_t len = strlen(needle);
    for (const char *p = strstr(hay, needle); p != NULL;
         p = strstr(p + len, needle)) {
        n++;
    }
    return n;
}

/* Number of elements in the k-th "turns_ms" array of `json`. */
static unsigned turns_in_array(const char *json, unsigned k) {
    const char *p = json;
    for (unsigned i = 0u; i <= k; i++) {
        p = strstr(p, "\"turns_ms\":[");
        if (p == NULL) {
            return 0u;
        }
        p += strlen("\"turns_ms\":[");
    }
    if (*p == ']') {
        return 0u;
    }
    unsigned n = 1u;
    for (; *p != ']' && *p != '\0'; p++) {
        n += (*p == ',') ? 1u : 0u;
    }
    return n;
}

/* The documents/04 game, played through the model with a tick every second:
 * limit 2:00; P1 62/95/71 s, P2 88/120/134 s, P3 45/51 s; a 30 s pause and a
 * settings visit inside P1's second turn; the uint32 clock wraps during P2's
 * first turn; the game ends while P2's third turn is paused. Returns now. */
static uint32_t play_doc_example(rc_game_t *g, unsigned *beeps) {
    uint32_t t = 4294900000u;
    *beeps = 0u;

    rc_game_init(g);
    rc_t_apply(g, ACT(OPEN_SETTINGS), t);
    rc_t_apply(g, ACT(MENU_DOWN), t);   /* row 0 is PLAYERS */
    for (unsigned i = 0u; i < 4u; i++) {
        rc_t_apply(g, ACT(MENU_NEXT), t);       /* Off -> 2:00 */
    }
    rc_t_apply(g, ACT(MENU_DONE), t);
    ASSERT_EQ(rc_settings_limit_ms(&g->settings), 120000u);

    rc_t_apply(g, ACT(START), t);
    *beeps += rc_t_advance(g, &t, 62000u);
    rc_t_apply(g, ACTS(SWITCH, 1), t);
    *beeps += rc_t_advance(g, &t, 88000u);
    rc_t_apply(g, ACTS(SWITCH, 2), t);
    *beeps += rc_t_advance(g, &t, 45000u);
    rc_t_apply(g, ACTS(SWITCH, 0), t);

    *beeps += rc_t_advance(g, &t, 40000u);
    rc_t_apply(g, ACT(PAUSE), t);
    *beeps += rc_t_advance(g, &t, 30000u);       /* paused: not counted */
    rc_t_apply(g, ACT(OPEN_SETTINGS), t);
    rc_t_apply(g, ACT(MENU_DONE), t);
    rc_t_apply(g, ACT(RESUME), t);
    *beeps += rc_t_advance(g, &t, 55000u);
    rc_t_apply(g, ACTS(SWITCH, 1), t);

    *beeps += rc_t_advance(g, &t, 120000u);      /* exactly at the limit */
    rc_t_apply(g, ACTS(SWITCH, 2), t);
    *beeps += rc_t_advance(g, &t, 51000u);
    rc_t_apply(g, ACTS(SWITCH, 0), t);
    *beeps += rc_t_advance(g, &t, 71000u);
    rc_t_apply(g, ACTS(SWITCH, 1), t);
    *beeps += rc_t_advance(g, &t, 134000u);

    rc_t_apply(g, ACT(PAUSE), t);
    *beeps += rc_t_advance(g, &t, 2000u);        /* the blue hold */
    ASSERT_EQ(rc_t_apply(g, ACT(END_GAME), t), RC_EVT_GAME_OVER);
    return t;
}

static void test_doc_example(void) {
    static rc_game_t g;
    static char small[RC_RESULT_JSON_MAX];
    rc_result_t r;
    unsigned beeps;
    uint32_t t = play_doc_example(&g, &beeps);

    /* P2 hit 2:00 exactly once and passed it once: two beeps. */
    ASSERT_EQ(beeps, 2u);
    ASSERT_EQ(g.state, RC_STATE_OVER);

    rc_result_build(&r, &g, t, 0x7f3a9c21e04b5d18ull);
    ASSERT_STREQ(r.game_id, "7f3a9c21e04b5d18");
    ASSERT_EQ(r.limit_ms, 120000u);
    ASSERT_EQ(r.global_ms, 666000u);
    ASSERT_EQ(r.player_count, 3u);
    ASSERT_EQ(r.players[0].seat, 1u);
    ASSERT_EQ(r.players[1].seat, 2u);
    ASSERT_EQ(r.players[2].seat, 3u);
    ASSERT_EQ(r.players[0].turn_count, 3u);
    ASSERT_EQ(r.players[1].turn_count, 3u);
    ASSERT_EQ(r.players[2].turn_count, 2u);
    ASSERT_TRUE(r.players[0].turns_ms == g.players[0].log.turns_ms);  /* borrowed */

    size_t n = rc_result_to_json(&r, g_json, sizeof g_json);
    ASSERT_EQ(n, strlen(k_doc_example));
    ASSERT_STREQ(g_json, k_doc_example);
    ASSERT_TRUE(strchr(g_json, ' ') == NULL);
    ASSERT_TRUE(strchr(g_json, '\n') == NULL);

    /* Building later changes nothing in OVER: the clocks are frozen. */
    rc_result_build(&r, &g, t + 3600000u, 0x7f3a9c21e04b5d18ull);
    ASSERT_EQ(rc_result_to_json(&r, small, sizeof small), n);
    ASSERT_STREQ(small, k_doc_example);

    /* Every capacity: 0 when it does not fit (buf[0] = NUL, nothing written
     * past cap), the full string from len + 1 up. */
    for (size_t cap = 0u; cap <= n + 2u; cap++) {
        int before = g_failures;
        memset(small, 0x7F, sizeof small);
        size_t got = rc_result_to_json(&r, small, cap);
        if (cap <= n) {
            ASSERT_EQ(got, 0u);
            if (cap > 0u) {
                ASSERT_EQ(small[0], '\0');
            } else {
                ASSERT_EQ((unsigned char)small[0], 0x7Fu);
            }
        } else {
            ASSERT_EQ(got, n);
            ASSERT_STREQ(small, k_doc_example);
        }
        for (size_t i = cap; i < n + 8u; i++) {
            ASSERT_EQ((unsigned char)small[i], 0x7Fu);
        }
        RC_CONTEXT_ON_FAIL(before, "cap %lu", (unsigned long)cap);
        if (g_failures != before) {
            break;
        }
    }
    ASSERT_EQ(rc_result_to_json(&r, NULL, 0u), 0u);
}

/* Limit off -> null; seats that never played are absent (invariant 7);
 * settings edits show up as names and keys. */
static void test_skipped_seats_and_null_limit(void) {
    static rc_game_t g;
    rc_result_t r;
    uint32_t t = 0u;
    static const char expect[] =
        "{\"schema\":\"rootclock.game_result\",\"schema_version\":1,"
        "\"game_id\":\"0000000000000000\","
        "\"device\":{\"app\":\"rootclock\",\"app_version\":\"002\"},"
        "\"ended_at\":null,\"turn_limit_ms\":null,\"global_ms\":4000,"
        "\"players\":["
        "{\"seat\":1,\"nickname\":\"Jacob\",\"faction\":\"lilypad\","
        "\"total_ms\":1500,\"turns_ms\":[1000,500],\"turns_truncated\":false},"
        "{\"seat\":3,\"nickname\":\"Lorenzo\",\"faction\":\"knaves\","
        "\"total_ms\":2500,\"turns_ms\":[2500],\"turns_truncated\":false}"
        "]}";

    rc_game_init(&g);
    rc_t_apply(&g, ACT(OPEN_SETTINGS), t);
    rc_t_apply(&g, ACT(MENU_DOWN), t);   /* row 0 is PLAYERS */
    rc_t_apply(&g, ACT(MENU_DOWN), t);
    rc_t_apply(&g, ACT(MENU_DOWN), t);               /* row 2: P1 faction */
    rc_t_apply(&g, ACT(MENU_PREV), t);               /* Marquise -> Lilypad */
    for (unsigned i = 0u; i < 3u; i++) {
        rc_t_apply(&g, ACT(MENU_DOWN), t);           /* row 5: P3 nickname */
    }
    rc_t_apply(&g, ACT(MENU_NEXT), t);               /* Ahmed -> Lorenzo */
    rc_t_apply(&g, ACT(MENU_DOWN), t);               /* row 6: P3 faction */
    for (unsigned i = 0u; i < 9u; i++) {
        rc_t_apply(&g, ACT(MENU_NEXT), t);           /* Woodland -> Knaves */
    }
    rc_t_apply(&g, ACT(MENU_DONE), t);

    rc_t_apply(&g, ACT(START), t);
    rc_t_advance(&g, &t, 1000u);
    rc_t_apply(&g, ACTS(SWITCH, 2), t);
    rc_t_advance(&g, &t, 2500u);
    rc_t_apply(&g, ACTS(SWITCH, 0), t);
    rc_t_advance(&g, &t, 500u);
    rc_t_apply(&g, ACT(PAUSE), t);
    rc_t_apply(&g, ACT(END_GAME), t);

    rc_result_build(&r, &g, t, 0u);
    ASSERT_EQ(r.player_count, 2u);
    ASSERT_EQ(r.limit_ms, 0u);
    ASSERT_EQ(rc_result_to_json(&r, g_json, sizeof g_json), strlen(expect));
    ASSERT_STREQ(g_json, expect);
}

/* A zero-length turn still counts as a turn taken. */
static void test_zero_length_turn(void) {
    static rc_game_t g;
    rc_result_t r;

    rc_game_init(&g);
    rc_t_apply(&g, ACT(START), 5000u);
    rc_t_apply(&g, ACT(PAUSE), 5000u);
    rc_t_apply(&g, ACT(END_GAME), 5000u);
    rc_result_build(&r, &g, 5000u, 1u);
    ASSERT_EQ(r.player_count, 1u);
    ASSERT_TRUE(rc_result_to_json(&r, g_json, sizeof g_json) > 0u);
    ASSERT_TRUE(strstr(g_json, "\"game_id\":\"0000000000000001\"") != NULL);
    ASSERT_TRUE(strstr(g_json, "\"global_ms\":0,") != NULL);
    ASSERT_TRUE(strstr(g_json, "\"players\":[{\"seat\":1,\"nickname\":\"Jacob\","
                               "\"faction\":\"marquise\",\"total_ms\":0,"
                               "\"turns_ms\":[0],\"turns_truncated\":false}]}")
                != NULL);
}

static void test_game_id(void) {
    static rc_game_t g;
    rc_result_t r;
    static const struct {
        uint64_t    bits;
        const char *hex;
    } ids[] = {
        { 0x0000000000000000ull, "0000000000000000" },
        { 0x0000000000000001ull, "0000000000000001" },
        { 0x8000000000000000ull, "8000000000000000" },
        { 0x0123456789ABCDEFull, "0123456789abcdef" },
        { 0xFFFFFFFFFFFFFFFFull, "ffffffffffffffff" },
        { 0x7f3a9c21e04b5d18ull, "7f3a9c21e04b5d18" },
    };

    rc_game_init(&g);
    rc_t_apply(&g, ACT(START), 0u);
    rc_t_apply(&g, ACT(PAUSE), 10u);
    rc_t_apply(&g, ACT(END_GAME), 10u);
    for (unsigned i = 0u; i < sizeof ids / sizeof ids[0]; i++) {
        rc_result_build(&r, &g, 10u, ids[i].bits);
        ASSERT_STREQ(r.game_id, ids[i].hex);
        ASSERT_EQ(strlen(r.game_id), RC_GAME_ID_HEX);
    }
}

/* 4 players x 130 turns of 8 000 000 ms: every log truncated, and global
 * (4 160 000 000) near the uint32 ceiling, which is the widest JSON a game
 * can produce. It must still fit RC_RESULT_JSON_MAX. */
static void test_truncated_worst_case(void) {
    static rc_game_t g;
    rc_result_t r;
    const uint32_t d = 8000000u;
    uint32_t t = 0x80000000u;

    rc_game_init(&g);
    rc_t_apply(&g, ACT(OPEN_SETTINGS), t);
    rc_t_apply(&g, ACT(MENU_DOWN), t);   /* row 0 is PLAYERS */
    rc_t_apply(&g, ACT(MENU_PREV), t);                /* Off -> 15:00 */
    rc_t_apply(&g, ACT(MENU_DONE), t);
    rc_t_apply(&g, ACT(START), t);
    for (unsigned round = 0u; round < 130u; round++) {
        for (uint8_t seat = 0u; seat < RC_SEATS; seat++) {
            if (round != 0u || seat != 0u) {
                rc_t_apply(&g, ACTS(SWITCH, seat), t);
            }
            t += d;
        }
    }
    rc_t_apply(&g, ACT(PAUSE), t);
    ASSERT_EQ(rc_t_apply(&g, ACT(END_GAME), t), RC_EVT_GAME_OVER);

    rc_result_build(&r, &g, t, 0xFFFFFFFFFFFFFFFFull);
    ASSERT_EQ(r.player_count, 4u);
    ASSERT_EQ(r.global_ms, 4160000000u);
    for (unsigned i = 0u; i < RC_SEATS; i++) {
        ASSERT_TRUE(r.players[i].truncated);
        ASSERT_EQ(r.players[i].turn_count, RC_MAX_TURNS);
        ASSERT_EQ(r.players[i].total_ms, 130u * d);
        ASSERT_EQ(r.players[i].turns_ms[RC_MAX_TURNS - 1u], 3u * d);
    }

    size_t n = rc_result_to_json(&r, g_json, sizeof g_json);
    ASSERT_TRUE(n > 0u);
    ASSERT_TRUE(n < RC_RESULT_JSON_MAX);
    ASSERT_EQ(strlen(g_json), n);
    ASSERT_EQ(count_substr(g_json, "\"turns_truncated\":true"), 4u);
    ASSERT_EQ(count_substr(g_json, "\"turns_truncated\":false"), 0u);
    ASSERT_TRUE(strstr(g_json, "\"turn_limit_ms\":900000,") != NULL);
    ASSERT_TRUE(strstr(g_json, "\"global_ms\":4160000000,") != NULL);
    ASSERT_TRUE(strstr(g_json, "\"total_ms\":1040000000,") != NULL);
    ASSERT_TRUE(strstr(g_json, ",24000000],") != NULL);   /* folded slot */
    for (unsigned k = 0u; k < RC_SEATS; k++) {
        ASSERT_EQ(turns_in_array(g_json, k), RC_MAX_TURNS);
    }
    printf("worst-case result JSON: %lu bytes (limit %u)\n",
           (unsigned long)n, (unsigned)RC_RESULT_JSON_MAX);
}

int main(void) {
    test_doc_example();
    test_skipped_seats_and_null_limit();
    test_zero_length_turn();
    test_game_id();
    test_truncated_worst_case();
    TEST_RETURN();
}
