/* rootclock -- display CPU: LCD, 5 buttons, 7 RGB LEDs, IR, mic, speaker,
 * accelerometer, RTC. Umbrella header: fwog_display.h. */
#include "fwog_display.h"
#include "pico/stdlib.h"

/* Red held 6 s powers the board off, with a countdown on the LED bar.
   Required: an app that declares no power policy does not link. */
FWOG_POWER_DEFAULT();

int main(void) {
    board_init();

    absolute_time_t next_beat = make_timeout_time_ms(1000);
    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        /* Once per iteration, and take buttons from its result -- never call
           fwog_buttons_poll() as well, the power-off hold depends on it. */
        const fwog_power_t power = fwog_power_poll(now);
        (void)power.buttons;

        if (time_reached(next_beat)) {
            next_beat = make_timeout_time_ms(1000);
            DIAG("[rootclock_display] alive\n");
        }
        /* Poll fast: a 1 s sleep here would be too coarse for the debouncer. */
        sleep_ms(2);
    }
}
