/* rootclock -- BatteryGauge. See rc_battery.h. */
#include "rc_battery.h"

#include <string.h>

/* Typical 1-cell Li-ion/LiPo resting voltage against charge, coarse but
 * monotonic. mV ascending. */
static const struct {
    uint16_t mv;
    uint16_t pct_x10;
} k_curve[] = {
    {3270u, 0u},    {3610u, 50u},   {3690u, 100u},  {3710u, 150u},
    {3730u, 200u},  {3750u, 250u},  {3770u, 300u},  {3790u, 350u},
    {3800u, 400u},  {3820u, 450u},  {3840u, 500u},  {3850u, 550u},
    {3870u, 600u},  {3910u, 650u},  {3950u, 700u},  {3980u, 750u},
    {4020u, 800u},  {4080u, 850u},  {4110u, 900u},  {4150u, 950u},
    {4200u, 1000u},
};
#define CURVE_N (sizeof k_curve / sizeof k_curve[0])

uint16_t rc_battery_pct_x10_from_mv(uint16_t mv) {
    if (mv <= k_curve[0].mv) return 0u;
    if (mv >= k_curve[CURVE_N - 1u].mv) return 1000u;
    for (unsigned i = 1u; i < CURVE_N; i++) {
        if (mv <= k_curve[i].mv) {
            const uint32_t m0 = k_curve[i - 1u].mv, m1 = k_curve[i].mv;
            const uint32_t p0 = k_curve[i - 1u].pct_x10, p1 = k_curve[i].pct_x10;
            return (uint16_t)(p0 + (p1 - p0) * (mv - m0) / (m1 - m0));
        }
    }
    return 1000u;
}

uint8_t rc_battery_percent_from_mv(uint16_t mv) {
    return (uint8_t)((rc_battery_pct_x10_from_mv(mv) + 5u) / 10u);
}

void rc_battery_init(rc_battery_t *b) { memset(b, 0, sizeof *b); }

static void hist_clear(rc_battery_t *b) {
    b->hist_n = 0u;
    b->hist_head = 0u;
}

static uint16_t smoothed_mv(const rc_battery_t *b) {
    return (uint16_t)((b->mv_x16 + 8u) / 16u);
}

void rc_battery_update(rc_battery_t *b, uint32_t now_ms, uint16_t vbat_mv,
                       bool usb, bool charging, bool full) {
    (void)full;   /* "USB in, not charging" covers full; kept for callers */
    if (vbat_mv < RC_BATT_VALID_MIN_MV) return;

    if (!b->have || usb != b->usb) {
        /* First reading, or USB came or went: the voltage jumps (charging
           lifts it), so restart the average and the slope from here. */
        b->mv_x16 = (uint32_t)vbat_mv * 16u;
        hist_clear(b);
    } else {
        /* EMA, alpha 1/8: at one reading every 5 s, a ~40 s time constant,
           enough to hide the ADC's 20 mV steps. */
        b->mv_x16 = b->mv_x16 - b->mv_x16 / 8u + (uint32_t)vbat_mv * 2u;
    }
    b->have = true;
    b->usb = usb;
    b->charging = charging;

    if (usb) return;   /* no time-left estimate while powered */
    if (b->hist_n == 0u || now_ms - b->last_hist_ms >= RC_BATT_SAMPLE_MS) {
        b->hist_ms[b->hist_head] = now_ms;
        b->hist_pct_x10[b->hist_head] = rc_battery_pct_x10_from_mv(smoothed_mv(b));
        b->hist_head = (uint8_t)((b->hist_head + 1u) % RC_BATT_HIST);
        if (b->hist_n < RC_BATT_HIST) b->hist_n++;
        b->last_hist_ms = now_ms;
    }
}

/* Drain in tenths of a percent per hour from the least-squares slope of the
 * history, or 0 if the history is too short or shows no drain. */
static uint32_t measured_drain_x10_per_h(const rc_battery_t *b) {
    if (b->hist_n < 3u) return 0u;
    const unsigned oldest = (b->hist_head + RC_BATT_HIST - b->hist_n) % RC_BATT_HIST;
    const uint32_t t0 = b->hist_ms[oldest];
    const unsigned newest = (b->hist_head + RC_BATT_HIST - 1u) % RC_BATT_HIST;
    if (b->hist_ms[newest] - t0 < RC_BATT_MIN_SPAN_MS) return 0u;

    /* x = seconds since the oldest point, y = pct x10. */
    int64_t sx = 0, sy = 0, sxx = 0, sxy = 0;
    const int64_t n = b->hist_n;
    for (unsigned k = 0u; k < b->hist_n; k++) {
        const unsigned i = (oldest + k) % RC_BATT_HIST;
        const int64_t x = (int64_t)((b->hist_ms[i] - t0) / 1000u);
        const int64_t y = b->hist_pct_x10[i];
        sx += x;
        sy += y;
        sxx += x * x;
        sxy += x * y;
    }
    const int64_t den = n * sxx - sx * sx;
    if (den <= 0) return 0u;
    const int64_t num = n * sxy - sx * sy;   /* slope = num/den, x10 pct per s */
    if (num >= 0) return 0u;                 /* flat or rising: no drain seen */
    const int64_t drain = (-num * 3600) / den;
    return drain > 0 ? (uint32_t)drain : 0u;
}

void rc_battery_view(const rc_battery_t *b, rc_battery_view_t *v) {
    memset(v, 0, sizeof *v);
    if (!b->have) {
        v->mode = RC_BATT_UNKNOWN;
        return;
    }
    v->mv = smoothed_mv(b);
    const uint16_t pct_x10 = rc_battery_pct_x10_from_mv(v->mv);
    v->percent = (uint8_t)((pct_x10 + 5u) / 10u);
    if (b->usb) {
        v->mode = b->charging ? RC_BATT_CHARGING : RC_BATT_ON_USB;
        return;
    }
    v->mode = RC_BATT_ON_BATTERY;

    uint32_t drain = measured_drain_x10_per_h(b);
    v->measured = drain >= RC_BATT_MIN_DRAIN_X10;
    if (!v->measured) drain = RC_BATT_NOMINAL_PCT_PER_H * 10u;
    v->remaining_min = (uint32_t)pct_x10 * 60u / drain;
    v->has_estimate = true;
}

size_t rc_battery_fmt_remaining(uint32_t minutes, char *out) {
    char *p = out;
    if (minutes >= 100u * 60u) {
        memcpy(out, "99h+", 5u);
        return 4u;
    }
    const uint32_t h = minutes / 60u, m = minutes % 60u;
    if (h > 0u) {
        if (h >= 10u) *p++ = (char)('0' + h / 10u);
        *p++ = (char)('0' + h % 10u);
        *p++ = 'h';
        *p++ = (char)('0' + m / 10u);
        *p++ = (char)('0' + m % 10u);
    } else {
        if (m >= 10u) *p++ = (char)('0' + m / 10u);
        *p++ = (char)('0' + m % 10u);
    }
    *p++ = 'm';
    *p = '\0';
    return (size_t)(p - out);
}
