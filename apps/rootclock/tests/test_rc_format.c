/* rootclock host tests -- screen time strings. */
#include "rc_test_support.h"

#include "rc_format.h"
#include "rc_settings.h"

typedef size_t (*rc_fmt_fn)(uint32_t ms, char *out);

/* Format into a guarded buffer: the result must match, the return value must
 * be its length, and nothing past RC_FMT_MAX may be written. */
static void check(rc_fmt_fn fn, const char *name, uint32_t ms,
                  const char *expect) {
    char buf[RC_FMT_MAX + 8u];
    int before = g_failures;
    memset(buf, 0x7F, sizeof buf);
    size_t n = fn(ms, buf);
    ASSERT_STREQ(buf, expect);
    ASSERT_EQ(n, strlen(expect));
    ASSERT_TRUE(strlen(expect) < RC_FMT_MAX);
    for (size_t i = RC_FMT_MAX; i < sizeof buf; i++) {
        ASSERT_EQ((unsigned char)buf[i], 0x7Fu);
    }
    RC_CONTEXT_ON_FAIL(before, "%s(%lu)", name, (unsigned long)ms);
}

#define TURN(ms, s)  check(rc_fmt_turn, "rc_fmt_turn", (ms), (s))
#define HMS(ms, s)   check(rc_fmt_hms, "rc_fmt_hms", (ms), (s))
#define LIMIT(ms, s) check(rc_fmt_limit, "rc_fmt_limit", (ms), (s))

static void test_turn(void) {
    TURN(0u, "00:00");
    TURN(999u, "00:00");                 /* floored */
    TURN(1999u, "00:01");
    TURN(59999u, "00:59");
    TURN(60000u, "01:00");
    TURN(605000u, "10:05");
    TURN(5999000u, "99:59");             /* 99:59 is the last MM:SS */
    TURN(5999999u, "99:59");
    TURN(6000000u, "1:40:00");           /* 100:00 switches to H:MM:SS */
    TURN(7199999u, "1:59:59");
    TURN(36000000u, "10:00:00");         /* multi-hour, hours unpadded */
    TURN(45296000u, "12:34:56");
    TURN(0xFFFFFFFFu, "1193:02:47");     /* the widest possible output */
}

static void test_hms(void) {
    HMS(0u, "0:00:00");
    HMS(999u, "0:00:00");
    HMS(59999u, "0:00:59");
    HMS(60000u, "0:01:00");
    HMS(3599999u, "0:59:59");
    HMS(3600000u, "1:00:00");
    HMS(5999000u, "1:39:59");
    HMS(45296000u, "12:34:56");
    HMS(360000000u, "100:00:00");
    HMS(0xFFFFFFFFu, "1193:02:47");
}

static void test_limit(void) {
    static const char *const labels[RC_LIMIT_OPTION_COUNT] = {
        "off", "0:30", "1:00", "1:30", "2:00", "3:00", "4:00", "5:00",
        "7:00", "10:00", "15:00",
    };
    for (uint8_t i = 0u; i < RC_LIMIT_OPTION_COUNT; i++) {
        LIMIT(rc_limit_option_ms(i), labels[i]);
    }
    LIMIT(0u, "off");
    LIMIT(1u, "0:00");
    LIMIT(59999u, "0:59");
    LIMIT(6000000u, "100:00");
    LIMIT(0xFFFFFFFFu, "71582:47");
}

int main(void) {
    test_turn();
    test_hms();
    test_limit();
    TEST_RETURN();
}
