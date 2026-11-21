/* rootclock -- BatteryGauge: charge and time left from the charger's
 * readings. Pure (no SDK); display/battery.c feeds it from the BQ25896.
 *
 * The FreeWili OG has no fuel gauge (the BQ27441 footprint is unpopulated,
 * see the BSP's docs/hardware/pinmap.md). The charger only measures the
 * battery voltage (BATV, 20 mV steps) and reports USB input and charge
 * state. So:
 *
 *   percent    from the smoothed voltage through a typical 1-cell Li-ion
 *              discharge curve (rc_battery_percent_from_mv). Approximate:
 *              the reading sags under load and rises while charging.
 *   time left  percent / drain rate. The rate is MEASURED once there are at
 *              least RC_BATT_MIN_SPAN_MS of discharge samples: a least-
 *              squares slope over the last RC_BATT_HIST minutes. Before
 *              that, or when the measured drain is too small to trust, it
 *              uses RC_BATT_NOMINAL_PCT_PER_H, an assumption (about 150 mA
 *              from a 1000 mAh cell) -- `measured` says which one is shown.
 *
 * Plugging in or unplugging USB clears the history, because the slope
 * before and after means nothing for the other. */
#ifndef RC_BATTERY_H
#define RC_BATTERY_H
#include "rc_types.h"

#define RC_BATT_SAMPLE_MS        60000u   /* one history point per minute  */
#define RC_BATT_HIST             32u      /* -> up to 31 minutes of slope  */
#define RC_BATT_MIN_SPAN_MS      600000u  /* 10 min before a measured rate */
#define RC_BATT_NOMINAL_PCT_PER_H 15u     /* assumed until measured        */
#define RC_BATT_MIN_DRAIN_X10    5u       /* 0.5 %/h: below this, no trust */
#define RC_BATT_VALID_MIN_MV     2500u    /* below: no cell / ADC not ready*/

typedef enum {
    RC_BATT_UNKNOWN = 0,   /* no valid reading yet                         */
    RC_BATT_ON_BATTERY,    /* discharging: percent and time left           */
    RC_BATT_CHARGING,      /* USB in, charge in progress                   */
    RC_BATT_ON_USB         /* USB in, not charging (full, or no charging)  */
} rc_batt_mode_t;

typedef struct {
    rc_batt_mode_t mode;
    uint16_t       mv;              /* smoothed battery voltage             */
    uint8_t        percent;         /* 0..100                               */
    bool           has_estimate;    /* remaining_min is meaningful          */
    bool           measured;        /* from the slope, not the assumption   */
    uint32_t       remaining_min;
} rc_battery_view_t;

typedef struct {
    bool     have;                  /* at least one valid reading           */
    bool     usb;                   /* last reading's USB / charge state    */
    bool     charging;
    uint32_t mv_x16;                /* EMA of the voltage, x16              */
    uint32_t last_hist_ms;          /* time of the newest history point     */
    uint8_t  hist_n;                /* points in use, <= RC_BATT_HIST       */
    uint8_t  hist_head;             /* next slot to write                   */
    uint32_t hist_ms[RC_BATT_HIST];
    uint16_t hist_pct_x10[RC_BATT_HIST];
} rc_battery_t;

void rc_battery_init(rc_battery_t *b);

/* One reading from the charger at now_ms. A vbat_mv below
 * RC_BATT_VALID_MIN_MV is ignored (the ADC's power-on value decodes to
 * 2304 mV). `usb` is VBUS present; `charging` is pre- or fast-charge;
 * `full` is charge done. */
void rc_battery_update(rc_battery_t *b, uint32_t now_ms, uint16_t vbat_mv,
                       bool usb, bool charging, bool full);

/* What the screen shows. */
void rc_battery_view(const rc_battery_t *b, rc_battery_view_t *v);

/* 1-cell Li-ion open-circuit curve, 3270 mV = 0 % .. 4200 mV = 100 %,
 * linear between table points; x10 (0..1000). */
uint16_t rc_battery_pct_x10_from_mv(uint16_t mv);
uint8_t  rc_battery_percent_from_mv(uint16_t mv);

/* "45m", "2h10m", "99h+" (minutes rounded down). `out` holds >= 8 bytes.
 * Returns the length. */
size_t rc_battery_fmt_remaining(uint32_t minutes, char *out);

#endif
