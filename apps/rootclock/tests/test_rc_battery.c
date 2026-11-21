/* rootclock host tests -- BatteryGauge: percent curve, smoothing, the
 * measured and assumed drain rates, and the time-left string. */
#include "rc_test_support.h"

#include "rc_battery.h"

#define MIN_MS 60000u

static void test_curve(void) {
    ASSERT_EQ(rc_battery_percent_from_mv(0u), 0u);
    ASSERT_EQ(rc_battery_percent_from_mv(3270u), 0u);
    ASSERT_EQ(rc_battery_percent_from_mv(4200u), 100u);
    ASSERT_EQ(rc_battery_percent_from_mv(4400u), 100u);    /* charging overshoot */
    ASSERT_EQ(rc_battery_percent_from_mv(3840u), 50u);     /* a table point */
    ASSERT_EQ(rc_battery_pct_x10_from_mv(3830u), 475u);    /* halfway 3820..3840 */
    /* Monotonic across the whole range in 20 mV ADC steps. */
    uint16_t prev = 0u;
    for (uint16_t mv = 2304u; mv <= 4844u; mv += 20u) {
        const uint16_t p = rc_battery_pct_x10_from_mv(mv);
        ASSERT_TRUE(p >= prev);
        ASSERT_TRUE(p <= 1000u);
        prev = p;
    }
}

static void test_unknown_and_invalid(void) {
    rc_battery_t b;
    rc_battery_view_t v;
    rc_battery_init(&b);
    rc_battery_view(&b, &v);
    ASSERT_EQ(v.mode, RC_BATT_UNKNOWN);
    ASSERT_TRUE(!v.has_estimate);
    /* The ADC's power-on value (2304 mV) is not a reading. */
    rc_battery_update(&b, 1000u, 2304u, false, false, false);
    rc_battery_view(&b, &v);
    ASSERT_EQ(v.mode, RC_BATT_UNKNOWN);
}

static void test_smoothing(void) {
    rc_battery_t b;
    rc_battery_view_t v;
    rc_battery_init(&b);
    rc_battery_update(&b, 0u, 3900u, false, false, false);
    rc_battery_view(&b, &v);
    ASSERT_EQ(v.mv, 3900u);
    /* One 20 mV ADC step barely moves it... */
    rc_battery_update(&b, 5000u, 3880u, false, false, false);
    rc_battery_view(&b, &v);
    ASSERT_TRUE(v.mv >= 3897u && v.mv <= 3898u);
    /* ...and a steady new level is reached within a few time constants. */
    for (uint32_t t = 10000u; t < 300000u; t += 5000u) {
        rc_battery_update(&b, t, 3880u, false, false, false);
    }
    rc_battery_view(&b, &v);
    ASSERT_EQ(v.mv, 3880u);
}

/* Nominal rate until 10 minutes of samples exist. */
static void test_assumed_rate(void) {
    rc_battery_t b;
    rc_battery_view_t v;
    rc_battery_init(&b);
    rc_battery_update(&b, 0u, 3840u, false, false, false);   /* 50 % */
    rc_battery_view(&b, &v);
    ASSERT_EQ(v.mode, RC_BATT_ON_BATTERY);
    ASSERT_EQ(v.percent, 50u);
    ASSERT_TRUE(v.has_estimate);
    ASSERT_TRUE(!v.measured);
    ASSERT_EQ(v.remaining_min, 50u * 60u / RC_BATT_NOMINAL_PCT_PER_H);   /* 200 */
}

/* Feed a cell that loses `pct_per_h` per hour, readings every 5 s, starting
 * at `start_mv` on the curve; returns the gauge after `minutes`. */
static void discharge(rc_battery_t *b, uint32_t start_pct_x10, uint32_t pct_x10_per_h,
                      uint32_t minutes) {
    rc_battery_init(b);
    for (uint32_t t = 0u; t <= minutes * MIN_MS; t += 5000u) {
        const uint32_t pct = start_pct_x10 - pct_x10_per_h * t / 3600000u;
        /* invert the curve by search: the lowest mV with at least pct */
        uint16_t mv = 3270u;
        while (mv < 4200u && rc_battery_pct_x10_from_mv(mv) < pct) mv++;
        mv = (uint16_t)(mv - mv % 20u);   /* the ADC's 20 mV steps */
        rc_battery_update(b, t, mv, false, false, false);
    }
}

static void test_measured_rate(void) {
    rc_battery_t b;
    rc_battery_view_t v;
    /* 30 %/h from 80 %: after 20 min the cell is near 70 % and the slope
       says about 30 %/h, so about 2h20m are left. */
    discharge(&b, 800u, 300u, 20u);
    rc_battery_view(&b, &v);
    ASSERT_TRUE(v.measured);
    ASSERT_TRUE(v.percent >= 68u && v.percent <= 72u);
    const uint32_t expect = (uint32_t)v.percent * 60u / 30u;
    ASSERT_TRUE(v.remaining_min + 25u >= expect && v.remaining_min <= expect + 25u);

    /* A flat cell (no measurable drain) falls back to the assumption. */
    discharge(&b, 600u, 0u, 20u);
    rc_battery_view(&b, &v);
    ASSERT_TRUE(!v.measured);
    ASSERT_TRUE(v.has_estimate);
}

static void test_usb(void) {
    rc_battery_t b;
    rc_battery_view_t v;
    discharge(&b, 800u, 300u, 20u);
    rc_battery_update(&b, 20u * MIN_MS + 5000u, 4150u, true, true, false);
    rc_battery_view(&b, &v);
    ASSERT_EQ(v.mode, RC_BATT_CHARGING);
    ASSERT_TRUE(!v.has_estimate);
    ASSERT_EQ(v.mv, 4150u);                       /* restarted, not averaged */
    rc_battery_update(&b, 20u * MIN_MS + 10000u, 4200u, true, false, true);
    rc_battery_view(&b, &v);
    ASSERT_EQ(v.mode, RC_BATT_ON_USB);
    /* Unplugged: history starts over, so the next estimate is assumed. */
    rc_battery_update(&b, 20u * MIN_MS + 15000u, 4100u, false, false, false);
    rc_battery_view(&b, &v);
    ASSERT_EQ(v.mode, RC_BATT_ON_BATTERY);
    ASSERT_TRUE(!v.measured);
    ASSERT_EQ(b.hist_n, 1u);
}

static void fmt(uint32_t minutes, const char *expect) {
    char buf[16];
    memset(buf, 0x7F, sizeof buf);
    const size_t n = rc_battery_fmt_remaining(minutes, buf);
    ASSERT_STREQ(buf, expect);
    ASSERT_EQ(n, strlen(expect));
    ASSERT_EQ((unsigned char)buf[8], 0x7Fu);
}

static void test_format(void) {
    fmt(0u, "0m");
    fmt(9u, "9m");
    fmt(45u, "45m");
    fmt(60u, "1h00m");
    fmt(130u, "2h10m");
    fmt(599u, "9h59m");
    fmt(600u, "10h00m");
    fmt(5999u, "99h59m");
    fmt(6000u, "99h+");
    fmt(0xFFFFFFFFu, "99h+");
}

int main(void) {
    test_curve();
    test_unknown_and_invalid();
    test_smoothing();
    test_assumed_rate();
    test_measured_rate();
    test_usb();
    test_format();
    TEST_RETURN();
}
