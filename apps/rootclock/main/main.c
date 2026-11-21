/* rootclock -- main CPU: watchdog, display update, and the relay between the
 * display and the Bottlenose WiFi Orca's ESP32-C6 (004).
 *
 * The relay logic is main/bridge.c (portable, host-tested). This file is the
 * board glue, adapted from apps/bottlenose/main/main.c:
 *
 *   C6 GPIO16 (U0TXD) -> header pin 5 -> GPIO 9 (UART1 RX)
 *   C6 GPIO17 (U0RXD) <- header pin 9 <- GPIO 8 (UART1 TX)
 *   115200 8N1, no flow control
 *
 * The C6 must run apps/bottlenose/c6/bottlenose_c6 1.4 or later, which knows
 * the RESULT and WIFI ON/OFF commands. Without a Bottlenose, nothing changes
 * for the game: the display keeps its results and says no WiFi module was
 * found.
 *
 * USB console (`fw console main`): "[c6] ..." for every line the C6 sends
 * (not its once-a-second TIME lines), "[to c6] ..." for what this CPU sends
 * (not each RESULT DATA line). Lines typed there go to the C6. Bridge
 * commands: !help, !verbose (show TIME and RESULT DATA too), !stats. */
#include "fwog_main.h"
#include "bridge.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include "rc_link.h"
#include <stdio.h>
#include <string.h>

/* board_init() arms an 8.3 s watchdog; this app kicks it every loop.
   Required: an app that declares no watchdog policy does not link. */
FWOG_WATCHDOG_DEFAULT();

#define C6_UART      uart1
#define C6_UART_IRQ  UART1_IRQ
#define C6_BAUD      115200u
#define C6_LINE_MAX  120u
/* ACKs and status frames are under 20 bytes framed, so they fit the
   display's 32-byte RX FIFO and never really wait; this is a backstop. */
#define LINK_SEND_TIMEOUT_US 2000u
#define REPORT_PERIOD_MS     10000u

/* ---- UART1 receive ring, filled from the interrupt (as in bottlenose) ---- */
#define RX_RING_SIZE 2048u   /* a power of two, so the counters may wrap */
static uint8_t           s_rx_ring[RX_RING_SIZE];
static volatile uint32_t s_rx_head;
static volatile uint32_t s_rx_tail;
static volatile uint32_t s_rx_overflow;

static void on_c6_rx(void) {
    while (uart_is_readable(C6_UART)) {
        const uint8_t c = (uint8_t)uart_getc(C6_UART);
        const uint32_t head = s_rx_head;
        if (head - s_rx_tail < RX_RING_SIZE) {
            s_rx_ring[head % RX_RING_SIZE] = c;
            s_rx_head = head + 1u;
        } else {
            s_rx_overflow++;
        }
    }
}

static bool c6_rx_pop(uint8_t *c) {
    const uint32_t tail = s_rx_tail;
    if (tail == s_rx_head) return false;
    *c = s_rx_ring[tail % RX_RING_SIZE];
    s_rx_tail = tail + 1u;
    return true;
}

static void c6_uart_init(void) {
    uart_init(C6_UART, C6_BAUD);
    uart_set_format(C6_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(C6_UART, false, false);
    uart_set_fifo_enabled(C6_UART, true);
    gpio_set_function(PIN_IO_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_IO_UART_RX, GPIO_FUNC_UART);
    irq_set_exclusive_handler(C6_UART_IRQ, on_c6_rx);
    irq_set_enabled(C6_UART_IRQ, true);
    uart_set_irq_enables(C6_UART, true, false);
}

/* ---- state ---- */
static rc_bridge_t    s_bridge;     /* holds a whole result: file scope   */
static fwog_link_rx_t s_link_rx;    /* ~4.2 KB: file scope, not stack     */
static bool           s_link_up;
static bool           s_verbose;
static uint32_t       s_link_drops;

/* ---- bridge I/O ---- */

/* No flow control on this UART, so a line takes at most ~11 ms. */
static void io_c6_line(const char *line, size_t len) {
    const bool data = len >= 12u && memcmp(line, "RESULT DATA ", 12u) == 0;
    if (s_verbose || !data) DIAG("[to c6] %.*s\n", (int)len, line);
    uart_write_blocking(C6_UART, (const uint8_t *)line, len);
    uart_putc_raw(C6_UART, '\n');
}

static bool io_link_send(const uint8_t *payload, size_t len) {
    if (!s_link_up) return false;
    const bool ok = rc_link_send(payload, len, LINK_SEND_TIMEOUT_US);
    if (!ok) s_link_drops++;
    return ok;
}

/* ---- C6 -> bridge ---- */

static void c6_line(const char *s, size_t len, uint32_t now) {
    /* A line of nothing but replaced bytes is line noise from a C6 reset. */
    size_t junk = 0u;
    while (junk < len && s[junk] == '?') junk++;
    if (junk == len) return;
    const bool tick = len >= 5u && memcmp(s, "TIME ", 5u) == 0;
    if (s_verbose || !tick) DIAG("[c6] %s\n", s);
    rc_bridge_on_c6_line(&s_bridge, s, now);
}

/* CR and LF end a line; ANSI escapes are stripped, NULs dropped, anything
   outside printable ASCII becomes '?'. */
static void c6_feed(uint8_t c, uint32_t now) {
    static char   line[C6_LINE_MAX + 1u];
    static size_t len;
    static enum { TXT, ESC, CSI } mode;
    if (mode == ESC) { mode = (c == '[') ? CSI : TXT; return; }
    if (mode == CSI) { if (c >= 0x40u && c <= 0x7Eu) mode = TXT; return; }
    if (c == 0x1Bu) { mode = ESC; return; }
    if (c == '\r' || c == '\n') {
        if (len > 0u) {
            line[len] = '\0';
            c6_line(line, len, now);
            len = 0u;
        }
        return;
    }
    if (c == '\0') return;
    if (c < 0x20u || c > 0x7Eu) c = '?';
    line[len++] = (char)c;
    if (len == C6_LINE_MAX) {
        line[len] = '\0';
        c6_line(line, len, now);
        len = 0u;
    }
}

/* ---- display -> bridge ---- */

static void link_poll(uint32_t now) {
    uint8_t b;
    while (fwog_link_uart_read(&b)) {
        size_t len = 0u;
        if (fwog_link_rx_byte(&s_link_rx, b, &len)) {
            rc_bridge_on_link(&s_bridge, s_link_rx.buf, len, now);
        }
    }
}

/* ---- USB console ---- */

static void print_stats(void) {
    const rc_uplink_status_t *st = &s_bridge.st;
    DIAG("[rootclock_main] c6: %s, %lu lines; wifi %d; result %d (http %u), "
         "c6 queue %u; lost: %lu rx bytes, %lu link sends\n",
         s_bridge.c6_heard ? "heard" : "SILENT", (unsigned long)s_bridge.lines_in,
         (int)st->wifi, (int)st->res, (unsigned)st->http, (unsigned)st->c6_queue,
         (unsigned long)s_rx_overflow, (unsigned long)s_link_drops);
}

static void usb_line(const char *s, size_t len) {
    if (strcmp(s, "!help") == 0) {
        DIAG("[rootclock_main] lines you type go to the C6 (STATUS, WIFI ON, "
             "WIFI OFF, QUEUE, SCAN, HELP). Bridge: !verbose, !stats\n");
    } else if (strcmp(s, "!verbose") == 0) {
        s_verbose = !s_verbose;
        DIAG("[rootclock_main] verbose %s\n", s_verbose ? "on" : "off");
    } else if (strcmp(s, "!stats") == 0) {
        print_stats();
    } else {
        io_c6_line(s, len);
    }
}

static void usb_poll(void) {
    static char   line[C6_LINE_MAX + 1u];
    static size_t len;
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\r' || c == '\n') {
            if (len > 0u) {
                line[len] = '\0';
                usb_line(line, len);
                len = 0u;
            }
        } else if (c == '\b' || c == 0x7F) {
            if (len > 0u) len--;
        } else if (c >= 0x20 && c <= 0x7E && len < C6_LINE_MAX) {
            line[len++] = (char)c;
        }
    }
}

int main(void) {
    board_init();

    /* Releases the display CPU, pushes the embedded display image if it
       differs from what the display has, then starts it. Never call
       board_release_display() yourself. */
    const fwog_display_result_t d = fwog_display_update_run();
    /* It brings the link up before it can fail any other way, so only these
       two leave UART0 unconfigured. */
    s_link_up = d != FWOG_DISP_LINK_DOWN && d != FWOG_DISP_IMAGE_BAD;
    fwog_link_rx_init(&s_link_rx);

    c6_uart_init();
    const rc_bridge_io_t io = { io_c6_line, io_link_send };
    rc_bridge_init(&s_bridge, &io, to_ms_since_boot(get_absolute_time()));

    /* USB CDC is not up during the first second or two, and output written
       before a host connects is dropped -- so repeat the banner for a while. */
    const absolute_time_t announce_until = make_timeout_time_ms(10000);
    absolute_time_t next_report = make_timeout_time_ms(1000);
    while (true) {
        board_watchdog_kick();      /* every iteration, or the board resets */
        const uint32_t now = to_ms_since_boot(get_absolute_time());

        uint8_t c;
        while (c6_rx_pop(&c)) c6_feed(c, now);
        if (s_link_up) link_poll(now);
        usb_poll();
        rc_bridge_poll(&s_bridge, now);

        if (time_reached(next_report)) {
            if (!time_reached(announce_until)) {
                next_report = make_timeout_time_ms(1000);
                DIAG("[rootclock_main] display: %s; C6 on UART1 GPIO 8/9. Type !help.\n",
                     fwog_display_result_text(d));
            } else {
                next_report = make_timeout_time_ms(REPORT_PERIOD_MS);
                if (!s_bridge.c6_heard || s_rx_overflow != 0u) print_stats();
            }
        }
    }
}
