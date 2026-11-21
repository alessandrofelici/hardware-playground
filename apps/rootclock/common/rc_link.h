/* rootclock -- sending one payload on the inter-CPU link without hanging.
 * Shared by both halves; the message formats are in core/rc_uplink.h.
 *
 * fwog_link_uart_send_frame() blocks behind RTS/CTS for as long as the peer
 * does not drain. On main that means a watchdog reset; on the display a frozen
 * loop (and red-6 s power-off stops working). This gives up instead: at once
 * if the TX FIFO is already full, else after `timeout_us`. A frame cut short
 * fails the receiver's CRC, and its decoder resynchronises on the next SOF.
 * The same pattern as apps/bottlenose/common/bn_link.h. */
#ifndef RC_LINK_H
#define RC_LINK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common/link/link_frame.h"
#include "hardware/uart.h"
#include "pico/time.h"
#include "platform/board.h"
#include "rc_uplink.h"

#define RC_LINK_FRAME_MAX (RC_CHUNK_HDR + RC_CHUNK_DATA_MAX + FWOG_LINK_OVERHEAD)

static inline bool rc_link_send(const uint8_t *payload, size_t len, uint32_t timeout_us) {
    uint8_t frame[RC_LINK_FRAME_MAX];
    const size_t n = fwog_link_encode(frame, sizeof frame, payload, len);
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
