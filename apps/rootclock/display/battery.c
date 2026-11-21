/* rootclock -- charger telemetry into the BatteryGauge. See battery.h. */
#include "battery.h"

#include "fwog_display.h"

#define ADC_REFRESH_MS   60000u   /* re-assert continuous conversion     */
#define FIRST_READ_MS    1500u    /* let the first conversion finish      */
#define BACKOFF_MS       60000u
#define FAILS_TO_BACKOFF 3u

static uint32_t s_next_read;
static uint32_t s_next_adc;
static uint8_t  s_fails;

void battery_init(uint32_t now_ms) {
    /* The charger's watchdog can drop it back to its defaults (one-shot ADC)
       when no host writes for a while, so battery_poll() re-asserts this
       every minute as well. */
    (void)bq25896_adc_continuous_enable();
    s_next_adc = now_ms + ADC_REFRESH_MS;
    s_next_read = now_ms + FIRST_READ_MS;
    s_fails = 0u;
}

bool battery_poll(rc_battery_t *b, uint32_t now_ms) {
    if ((int32_t)(now_ms - s_next_read) < 0) return false;
    s_next_read = now_ms + (s_fails >= FAILS_TO_BACKOFF ? BACKOFF_MS : BATTERY_PERIOD_MS);
    if ((int32_t)(now_ms - s_next_adc) >= 0) {
        (void)bq25896_adc_continuous_enable();
        s_next_adc = now_ms + ADC_REFRESH_MS;
    }

    bq25896_stat_t st;
    uint16_t vbat_mv = 0u, vbus_mv = 0u;
    bool thermal = false, vbus_attached = false;
    if (!bq25896_read_stat(&st) || !bq25896_read_vbat_mv(&vbat_mv, &thermal) ||
        !bq25896_read_vbus_mv(&vbus_mv, &vbus_attached)) {
        if (s_fails < 255u) s_fails++;
        return false;
    }
    s_fails = 0u;
    const bool usb = vbus_attached || st.vbus_stat != BQ25896_VBUS_NO_INPUT;
    const bool charging = st.chg_stat == BQ25896_CHG_PRE_CHARGE ||
                          st.chg_stat == BQ25896_CHG_FAST_CHARGE;
    rc_battery_update(b, now_ms, vbat_mv, usb, charging,
                      st.chg_stat == BQ25896_CHG_DONE);
    return true;
}
