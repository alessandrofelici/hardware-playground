/* rootclock -- reads the BQ25896 charger into a BatteryGauge (core/
 * rc_battery.h). Telemetry only: this never writes a charge setting and
 * never goes near CTRL1/BATFET_DIS (see the BSP's power/bq25896.h).
 *
 *   battery_init()   once after board_init(): turns on the charger's
 *                    continuous ADC (ADC_CTRL bits 7:6, nothing else).
 *   battery_poll()   every loop iteration; reads three registers (status,
 *                    BATV, VBUSV) every BATTERY_PERIOD_MS, and backs off to
 *                    once a minute after three failed reads in a row, so a
 *                    wedged I2C bus costs at most 3 x 10 ms per minute.
 *                    Returns true when the gauge changed. */
#ifndef BATTERY_H
#define BATTERY_H
#include <stdbool.h>
#include <stdint.h>
#include "rc_battery.h"

#define BATTERY_PERIOD_MS 5000u

void battery_init(uint32_t now_ms);
bool battery_poll(rc_battery_t *b, uint32_t now_ms);

#endif
