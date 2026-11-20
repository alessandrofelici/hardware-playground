/* rootclock -- main CPU: two CC1101 sub-GHz radios, iCE40 FPGA, breakout I/O.
 * Umbrella header: fwog_main.h. */
#include "fwog_main.h"
#include "pico/stdlib.h"

/* board_init() arms an 8.3 s watchdog; this app kicks it every loop.
   Required: an app that declares no watchdog policy does not link. */
FWOG_WATCHDOG_DEFAULT();

int main(void) {
    board_init();

    /* Releases the display CPU, pushes the embedded display image if it
       differs from what the display has, then starts it. Never call
       board_release_display() yourself. */
    const fwog_display_result_t d = fwog_display_update_run();

    /* USB CDC is not up during the first second or two, and output written
       before a host connects is dropped -- so repeat the result for a while. */
    const absolute_time_t announce_until = make_timeout_time_ms(10000);
    while (true) {
        board_watchdog_kick();      /* every iteration, or the board resets */
        if (!time_reached(announce_until)) {
            DIAG("[rootclock_main] display: %s\n", fwog_display_result_text(d));
        } else {
            DIAG("[rootclock_main] alive\n");
        }
        sleep_ms(500);
    }
}
