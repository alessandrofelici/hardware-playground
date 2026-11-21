/* rootclock host tests -- catalog, Settings, SettingsMenu, TurnLimitAlarm. */
#include "rc_test_support.h"

#include "rc_alarm.h"
#include "rc_catalog.h"
#include "rc_settings.h"

static void test_catalog(void) {
    /* documents/04 section 2, in rc_faction_id_t order. */
    static const char *const keys[RC_FACTION_COUNT] = {
        "marquise", "eyrie", "woodland", "vagabond", "riverfolk", "cult",
        "duchy", "corvid", "hundreds", "keepers", "twilight", "knaves",
        "lilypad",
    };
    static const char *const shorts[RC_FACTION_COUNT] = {
        "Marquise", "Eyrie", "Woodland", "Vagabond", "Riverfolk",
        "Lizard Cult", "Duchy", "Corvids", "Hundreds", "Keepers", "Twilight",
        "Knaves", "Lilypad",
    };
    static const char *const fulls[RC_FACTION_COUNT] = {
        "Marquise de Cat", "Eyrie Dynasties", "Woodland Alliance", "Vagabond",
        "Riverfolk Company", "Lizard Cult", "Underground Duchy",
        "Corvid Conspiracy", "Lord of the Hundreds", "Keepers in Iron",
        "Twilight Council", "Knaves of the Deepwood", "Lilypad Diaspora",
    };
    for (uint8_t i = 0u; i < RC_FACTION_COUNT; i++) {
        int before = g_failures;
        const rc_faction_t *f = rc_faction(i);
        ASSERT_TRUE(f != NULL);
        if (f == NULL) {
            continue;
        }
        ASSERT_STREQ(f->key, keys[i]);
        ASSERT_STREQ(f->short_name, shorts[i]);
        ASSERT_STREQ(f->full_name, fulls[i]);
        ASSERT_TRUE(strlen(f->short_name) <= 11u);   /* fits S1 at scale 2 */
        RC_CONTEXT_ON_FAIL(before, "faction %u", (unsigned)i);
    }
    ASSERT_EQ(RC_FACTION_COUNT, 13u);
    ASSERT_TRUE(rc_faction(RC_FACTION_COUNT) == NULL);
    ASSERT_TRUE(rc_faction(0xFFu) == NULL);

    ASSERT_STREQ(rc_nickname(0u), "Jacob");
    ASSERT_STREQ(rc_nickname(1u), "Alex");
    ASSERT_STREQ(rc_nickname(2u), "Ahmed");
    ASSERT_STREQ(rc_nickname(3u), "Lorenzo");
    ASSERT_STREQ(rc_nickname(4u), "?");
    ASSERT_STREQ(rc_nickname(0xFFu), "?");
}

static void test_settings_defaults(void) {
    rc_settings_t s;
    memset(&s, 0xA5, sizeof s);
    rc_settings_defaults(&s);
    ASSERT_EQ(s.players, 4u);
    ASSERT_EQ(s.limit_idx, 0u);
    ASSERT_EQ(rc_settings_limit_ms(&s), 0u);
    for (uint8_t i = 0u; i < RC_SEATS; i++) {
        ASSERT_EQ(s.seat[i].nickname_id, i);
    }
    ASSERT_EQ(s.seat[0].faction_id, RC_FACTION_MARQUISE);
    ASSERT_EQ(s.seat[1].faction_id, RC_FACTION_EYRIE);
    ASSERT_EQ(s.seat[2].faction_id, RC_FACTION_WOODLAND);
    ASSERT_EQ(s.seat[3].faction_id, RC_FACTION_VAGABOND);
}

static void test_limit_options(void) {
    static const uint32_t expect[RC_LIMIT_OPTION_COUNT] = {
        0u, 30000u, 60000u, 90000u, 120000u, 180000u, 240000u, 300000u,
        420000u, 600000u, 900000u,
    };
    rc_settings_t s;

    ASSERT_EQ(RC_LIMIT_OPTION_COUNT, 11u);
    for (uint8_t i = 0u; i < RC_LIMIT_OPTION_COUNT; i++) {
        ASSERT_EQ(rc_limit_option_ms(i), expect[i]);
    }
    ASSERT_EQ(rc_limit_option_ms(RC_LIMIT_OPTION_COUNT), 0u);
    ASSERT_EQ(rc_limit_option_ms(0xFFu), 0u);

    /* +1 visits every option in order and wraps back to Off. */
    rc_settings_defaults(&s);
    for (uint8_t i = 1u; i <= RC_LIMIT_OPTION_COUNT; i++) {
        rc_settings_cycle_limit(&s, +1);
        ASSERT_EQ(s.limit_idx, i % RC_LIMIT_OPTION_COUNT);
        ASSERT_EQ(rc_settings_limit_ms(&s), expect[i % RC_LIMIT_OPTION_COUNT]);
    }
    /* -1 from Off wraps to 15:00, and back. */
    rc_settings_cycle_limit(&s, -1);
    ASSERT_EQ(s.limit_idx, 10u);
    ASSERT_EQ(rc_settings_limit_ms(&s), 900000u);
    rc_settings_cycle_limit(&s, +1);
    ASSERT_EQ(s.limit_idx, 0u);

    /* dir is a sign, not a step; 0 is a no-op. */
    rc_settings_cycle_limit(&s, +5);
    ASSERT_EQ(s.limit_idx, 1u);
    rc_settings_cycle_limit(&s, -7);
    ASSERT_EQ(s.limit_idx, 0u);
    rc_settings_cycle_limit(&s, 0);
    ASSERT_EQ(s.limit_idx, 0u);

    /* A corrupt index reads as Off. */
    s.limit_idx = 200u;
    ASSERT_EQ(rc_settings_limit_ms(&s), 0u);
}

static bool is_permutation(const rc_settings_t *s) {
    uint8_t seen = 0u;
    for (uint8_t i = 0u; i < RC_SEATS; i++) {
        if (s->seat[i].nickname_id >= RC_NICKNAME_COUNT) {
            return false;
        }
        seen |= (uint8_t)(1u << s->seat[i].nickname_id);
    }
    return seen == 0x0Fu;
}

static void test_nickname_swap(void) {
    rc_settings_t s, before;

    /* AT-03: P1 Jacob -> Alex, and P2 (who held Alex) becomes Jacob. */
    rc_settings_defaults(&s);
    rc_settings_cycle_nickname(&s, 0u, +1);
    ASSERT_EQ(s.seat[0].nickname_id, 1u);
    ASSERT_EQ(s.seat[1].nickname_id, 0u);
    ASSERT_EQ(s.seat[2].nickname_id, 2u);
    ASSERT_EQ(s.seat[3].nickname_id, 3u);

    /* Wrap down: Jacob -1 -> Lorenzo, swapping with P4. */
    rc_settings_defaults(&s);
    rc_settings_cycle_nickname(&s, 0u, -1);
    ASSERT_EQ(s.seat[0].nickname_id, 3u);
    ASSERT_EQ(s.seat[3].nickname_id, 0u);
    ASSERT_EQ(s.seat[1].nickname_id, 1u);
    ASSERT_EQ(s.seat[2].nickname_id, 2u);

    /* Wrap up: Lorenzo +1 -> Jacob. */
    rc_settings_defaults(&s);
    rc_settings_cycle_nickname(&s, 3u, +1);
    ASSERT_EQ(s.seat[3].nickname_id, 0u);
    ASSERT_EQ(s.seat[0].nickname_id, 3u);

    /* No-ops: dir 0, seat out of range. Factions and limit untouched. */
    rc_settings_defaults(&s);
    memcpy(&before, &s, sizeof s);
    rc_settings_cycle_nickname(&s, 1u, 0);
    rc_settings_cycle_nickname(&s, RC_SEATS, +1);
    rc_settings_cycle_nickname(&s, RC_NO_SEAT, -1);
    ASSERT_TRUE(memcmp(&before, &s, sizeof s) == 0);

    /* Random walk: always a permutation, the moved seat lands on
     * (old + dir) mod 4, and only one other seat changes. */
    uint32_t lcg = 12345u;
    rc_settings_defaults(&s);
    for (unsigned step = 0u; step < 20000u; step++) {
        lcg = lcg * 1664525u + 1013904223u;
        uint8_t seat = (uint8_t)((lcg >> 24) & 3u);
        int dir = ((lcg >> 16) & 1u) ? +1 : -1;
        memcpy(&before, &s, sizeof s);
        rc_settings_cycle_nickname(&s, seat, dir);

        int fails = g_failures;
        uint8_t old = before.seat[seat].nickname_id;
        uint8_t want = (uint8_t)((old + (dir > 0 ? 1u : 3u)) % 4u);
        ASSERT_TRUE(is_permutation(&s));
        ASSERT_EQ(s.seat[seat].nickname_id, want);
        unsigned changed = 0u;
        for (uint8_t i = 0u; i < RC_SEATS; i++) {
            if (i != seat && s.seat[i].nickname_id != before.seat[i].nickname_id) {
                changed++;
                ASSERT_EQ(before.seat[i].nickname_id, want);
                ASSERT_EQ(s.seat[i].nickname_id, old);
            }
            ASSERT_EQ(s.seat[i].faction_id, before.seat[i].faction_id);
        }
        ASSERT_EQ(changed, 1u);
        RC_CONTEXT_ON_FAIL(fails, "walk step %u seat %u dir %d", step,
                           (unsigned)seat, dir);
        if (g_failures != fails) {
            break;
        }
    }
}

static void test_faction_cycle(void) {
    rc_settings_t s;

    rc_settings_defaults(&s);
    rc_settings_cycle_faction(&s, 0u, -1);
    ASSERT_EQ(s.seat[0].faction_id, RC_FACTION_LILYPAD);
    rc_settings_cycle_faction(&s, 0u, +1);
    ASSERT_EQ(s.seat[0].faction_id, RC_FACTION_MARQUISE);

    /* +1 thirteen times visits every faction once and comes back. */
    for (uint8_t i = 1u; i <= RC_FACTION_COUNT; i++) {
        rc_settings_cycle_faction(&s, 0u, +1);
        ASSERT_EQ(s.seat[0].faction_id, i % RC_FACTION_COUNT);
    }
    /* Other seats untouched; nicknames untouched. */
    ASSERT_EQ(s.seat[1].faction_id, RC_FACTION_EYRIE);
    ASSERT_EQ(s.seat[2].faction_id, RC_FACTION_WOODLAND);
    ASSERT_EQ(s.seat[3].faction_id, RC_FACTION_VAGABOND);
    ASSERT_TRUE(is_permutation(&s));

    /* Factions need not be unique (two Vagabonds is legal Root). */
    rc_settings_cycle_faction(&s, 1u, -1);
    ASSERT_EQ(s.seat[1].faction_id, RC_FACTION_MARQUISE);
    ASSERT_EQ(s.seat[0].faction_id, RC_FACTION_MARQUISE);

    /* No-ops. */
    rc_settings_cycle_faction(&s, 2u, 0);
    rc_settings_cycle_faction(&s, RC_SEATS, +1);
    ASSERT_EQ(s.seat[2].faction_id, RC_FACTION_WOODLAND);
}

static void test_menu(void) {
    static const rc_row_kind_t kinds[RC_MENU_ROWS] = {
        RC_ROW_PLAYERS, RC_ROW_LIMIT,
        RC_ROW_NICKNAME, RC_ROW_FACTION, RC_ROW_NICKNAME, RC_ROW_FACTION,
        RC_ROW_NICKNAME, RC_ROW_FACTION, RC_ROW_NICKNAME, RC_ROW_FACTION,
    };
    static const uint8_t seats[RC_MENU_ROWS] = {
        RC_NO_SEAT, RC_NO_SEAT, 0u, 0u, 1u, 1u, 2u, 2u, 3u, 3u,
    };
    rc_menu_t m;
    rc_settings_t s, before;

    ASSERT_EQ(RC_MENU_ROWS, 10u);
    for (uint8_t r = 0u; r < RC_MENU_ROWS; r++) {
        ASSERT_EQ(rc_menu_row_kind(r), kinds[r]);
        ASSERT_EQ(rc_menu_row_seat(r), seats[r]);
    }
    ASSERT_EQ(rc_menu_row_kind(RC_MENU_ROWS), RC_ROW_LIMIT);   /* seatless */
    ASSERT_EQ(rc_menu_row_seat(RC_MENU_ROWS), RC_NO_SEAT);
    ASSERT_EQ(rc_menu_row_seat(0xFFu), RC_NO_SEAT);

    m.row = 5u;
    rc_menu_reset(&m);
    ASSERT_EQ(m.row, 0u);
    rc_menu_move(&m, -1);
    ASSERT_EQ(m.row, 9u);
    rc_menu_move(&m, +1);
    ASSERT_EQ(m.row, 0u);
    for (uint8_t i = 1u; i <= RC_MENU_ROWS; i++) {
        rc_menu_move(&m, +1);
        ASSERT_EQ(m.row, i % RC_MENU_ROWS);
    }
    rc_menu_move(&m, 0);
    ASSERT_EQ(m.row, 0u);

    rc_settings_defaults(&s);

    /* AT-02: row 5 is P2's faction; Eyrie +4 = Lizard Cult. */
    m.row = 5u;
    for (unsigned i = 0u; i < 4u; i++) {
        rc_menu_change(&m, &s, +1);
    }
    ASSERT_EQ(s.seat[1].faction_id, RC_FACTION_CULT);

    /* AT-03: row 2 is P1's nickname; +1 swaps Jacob and Alex. */
    m.row = 2u;
    rc_menu_change(&m, &s, +1);
    ASSERT_EQ(s.seat[0].nickname_id, 1u);
    ASSERT_EQ(s.seat[1].nickname_id, 0u);

    /* AT-04: row 1 is the limit; +1 = 0:30. Row 0 is PLAYERS; 4 +1 = 3. */
    m.row = 1u;
    rc_menu_change(&m, &s, +1);
    ASSERT_EQ(rc_settings_limit_ms(&s), 30000u);
    m.row = 0u;
    rc_menu_change(&m, &s, +1);
    ASSERT_EQ(rc_settings_players(&s), 3u);
    ASSERT_EQ(rc_settings_limit_ms(&s), 30000u);   /* untouched */

    /* Row 9 is P4's faction; row 8 P4's nickname. */
    m.row = 9u;
    rc_menu_change(&m, &s, -1);
    ASSERT_EQ(s.seat[3].faction_id, RC_FACTION_WOODLAND);
    m.row = 8u;
    rc_menu_change(&m, &s, -1);
    ASSERT_EQ(s.seat[3].nickname_id, 2u);   /* Lorenzo -> Ahmed */
    ASSERT_EQ(s.seat[2].nickname_id, 3u);   /* swapped */
    ASSERT_TRUE(is_permutation(&s));

    /* An out-of-range cursor changes nothing. */
    memcpy(&before, &s, sizeof s);
    m.row = RC_MENU_ROWS;
    rc_menu_change(&m, &s, +1);
    ASSERT_TRUE(memcmp(&before, &s, sizeof s) == 0);
}

static void test_players(void) {
    rc_settings_t s;

    rc_settings_defaults(&s);
    ASSERT_EQ(s.players, 4u);
    ASSERT_EQ(rc_settings_players(&s), 4u);

    /* 4 -> 3 -> 4 in both directions; dir is a sign; 0 is a no-op. */
    rc_settings_cycle_players(&s, +1);
    ASSERT_EQ(rc_settings_players(&s), 3u);
    rc_settings_cycle_players(&s, +1);
    ASSERT_EQ(rc_settings_players(&s), 4u);
    rc_settings_cycle_players(&s, -1);
    ASSERT_EQ(rc_settings_players(&s), 3u);
    rc_settings_cycle_players(&s, -5);
    ASSERT_EQ(rc_settings_players(&s), 4u);
    rc_settings_cycle_players(&s, 0);
    ASSERT_EQ(rc_settings_players(&s), 4u);

    /* Corrupt values clamp into 3..4, and cycling recovers. */
    s.players = 0u;
    ASSERT_EQ(rc_settings_players(&s), 3u);
    s.players = 200u;
    ASSERT_EQ(rc_settings_players(&s), 4u);
    rc_settings_cycle_players(&s, +1);
    ASSERT_EQ(s.players, 3u);
}

static void test_alarm(void) {
    rc_alarm_t a;

    a.fired_this_turn = true;
    rc_alarm_reset(&a);
    ASSERT_TRUE(!a.fired_this_turn);

    ASSERT_TRUE(!rc_alarm_over(0u, 0u));
    ASSERT_TRUE(!rc_alarm_over(0xFFFFFFFFu, 0u));     /* limit off */
    ASSERT_TRUE(!rc_alarm_over(29999u, 30000u));
    ASSERT_TRUE(rc_alarm_over(30000u, 30000u));
    ASSERT_TRUE(rc_alarm_over(30001u, 30000u));

    /* Fires once, on the crossing. */
    ASSERT_TRUE(!rc_alarm_check(&a, 29999u, 30000u, true));
    ASSERT_TRUE(rc_alarm_check(&a, 30000u, 30000u, true));
    ASSERT_TRUE(a.fired_this_turn);
    ASSERT_TRUE(!rc_alarm_check(&a, 30001u, 30000u, true));
    ASSERT_TRUE(!rc_alarm_check(&a, 999999u, 30000u, true));

    /* Only while running. */
    rc_alarm_reset(&a);
    ASSERT_TRUE(!rc_alarm_check(&a, 40000u, 30000u, false));
    ASSERT_TRUE(!a.fired_this_turn);
    ASSERT_TRUE(rc_alarm_check(&a, 40000u, 30000u, true));

    /* Never with the limit off. */
    rc_alarm_reset(&a);
    ASSERT_TRUE(!rc_alarm_check(&a, 0xFFFFFFFFu, 0u, true));

    /* sync: already past a new limit -> fired, so red without a beep. */
    rc_alarm_reset(&a);
    rc_alarm_sync(&a, 45000u, 30000u);
    ASSERT_TRUE(a.fired_this_turn);
    ASSERT_TRUE(!rc_alarm_check(&a, 46000u, 30000u, true));
    ASSERT_TRUE(rc_alarm_over(46000u, 30000u));

    /* sync: under the new limit -> armed again. */
    rc_alarm_sync(&a, 10000u, 30000u);
    ASSERT_TRUE(!a.fired_this_turn);
    ASSERT_TRUE(rc_alarm_check(&a, 30000u, 30000u, true));

    /* sync to Off clears it. */
    rc_alarm_sync(&a, 99999u, 0u);
    ASSERT_TRUE(!a.fired_this_turn);
}

int main(void) {
    test_catalog();
    test_settings_defaults();
    test_limit_options();
    test_nickname_swap();
    test_faction_cycle();
    test_menu();
    test_players();
    test_alarm();
    TEST_RETURN();
}
