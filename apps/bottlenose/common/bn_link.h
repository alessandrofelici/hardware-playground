/* bottlenose -- the link messages the two halves exchange, and a send that
 * cannot hang.
 *
 * Types start at 0x40: 0x00 is invalid, 0x01-0x1F belong to the display
 * bootloader and 0x20-0x22 to the BSP's breakout-direction messages
 * (common/link/io_proto.h).
 *
 *   0x40 BN_MSG_LINE  main -> display  one line of text the ESP32-C6 sent
 *   0x41 BN_MSG_CMD   display -> main  one command line for the ESP32-C6
 *
 * Payload: [type][text], no terminator. Main sanitises C6 text to printable
 * ASCII (the LCD font's range) before sending. At most BN_TEXT_MAX bytes. */
#ifndef BN_LINK_H
#define BN_LINK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "common/link/link_frame.h"
#include "hardware/uart.h"
#include "pico/time.h"
#include "platform/board.h"

#define BN_MSG_LINE 0x40u
#define BN_MSG_CMD  0x41u
#define BN_TEXT_MAX 120u

/* fwog_link_uart_send_frame() is a blocking write behind RTS/CTS: if the peer
 * stops draining (a dead display, or the gap between its bootloader and its
 * app) it waits forever -- on main that means a watchdog reset every 8.3 s.
 * This gives up instead: at once if the TX FIFO is already full, else after
 * `timeout_us`. A frame cut short is rejected by the receiver's CRC and its
 * decoder resynchronises on a later SOF. */
static inline bool bn_link_send(uint8_t type, const char *text, size_t len,
                                uint32_t timeout_us) {
    uint8_t payload[1u + BN_TEXT_MAX];
    uint8_t frame[sizeof payload + FWOG_LINK_OVERHEAD];
    if (len > BN_TEXT_MAX) len = BN_TEXT_MAX;
    payload[0] = type;
    memcpy(&payload[1], text, len);
    const size_t n = fwog_link_encode(frame, sizeof frame, payload, len + 1u);
    if (n == 0u) return false;
    if (!uart_is_writable(FWOG_LINK_UART)) return false;

    const absolute_time_t deadline = make_timeout_time_us(timeout_us);
    for (size_t i = 0; i < n; i++) {
        while (!uart_is_writable(FWOG_LINK_UART)) {
            if (time_reached(deadline)) return false;
        }
        uart_putc_raw(FWOG_LINK_UART, frame[i]);
    }
    return true;
}

#endif
