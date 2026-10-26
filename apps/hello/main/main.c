/* hello -- main CPU: bring the display up, then blink the status LED. */
#include "fwog_main.h"
#include "pico/stdlib.h"

FWOG_WATCHDOG_DEFAULT();

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();

    /* Output written before a host opens the USB port is dropped, so the
       display result is repeated for the first 10 s. */
    const absolute_time_t announce_until = make_timeout_time_ms(10000);
    bool led = false;
    while (true) {
        board_watchdog_kick();
        led = !led;
        gpio_put(PIN_LED, led);
        if (!time_reached(announce_until)) {
            DIAG("[hello_main] display: %s\n", fwog_display_result_text(d));
        } else {
            DIAG("[hello_main] alive\n");
        }
        sleep_ms(500);
    }
}
