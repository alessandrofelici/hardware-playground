/* bottlenose -- main CPU: a bridge to the Bottlenose WiFi Orca's ESP32-C6.
 *
 * The Bottlenose plugs into the 20-pin breakout header, and its ESP32-C6
 * talks on this CPU's breakout UART1:
 *
 *   C6 GPIO16 (U0TXD) -> header pin 5 -> GPIO 9 (UART1 RX)
 *   C6 GPIO17 (U0RXD) <- header pin 9 <- GPIO 8 (UART1 TX)
 *   115200 8N1, no flow control
 *
 * That wiring comes from a community project built on this same BSP
 * (github.com/nullswitch23/fwog_defcon, firmware/bottlenose/README.md).
 * FreeWili publishes no Bottlenose pinout.
 *
 * Every line the C6 sends is printed on this CPU's USB console as "[c6] ..."
 * (`fw console main`) and forwarded to the display over the link
 * (BN_MSG_LINE). Every line typed on that console, and every BN_MSG_CMD from
 * the display's buttons, goes to the C6. Lines starting with '!' are this
 * bridge's own commands: !help, !verbose, !stats.
 *
 * GPIO16/17 are also the C6's boot UART, so with no C6 firmware of our own,
 * pressing RESET on the Bottlenose should still print its ROM boot banner
 * here. That proves the header, the level shifters and UART1.
 *
 * Breakout directions stay at their power-on default, which already has
 * GPIO 8 as an output and GPIO 9 as an input. fwog_io_dir_apply() is not
 * called: changing directions is the part of the breakout path the BSP lists
 * as not yet verified on hardware (tools/io_walk.py), and nothing here needs
 * a different direction.
 *
 * GPIO 25 is left alone. It is PIN_LED, but it also drives breakout header
 * pin 17, and what that pin does on the Bottlenose is undocumented. */
#include "fwog_main.h"
#include "bn_link.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

/* board_init() arms an 8.3 s watchdog; this app kicks it every loop. */
FWOG_WATCHDOG_DEFAULT();

#define C6_UART      uart1
#define C6_UART_IRQ  UART1_IRQ
#define C6_BAUD      115200u

/* How long a line may wait for the display to drain the link. The display
   redraws between polls, so this covers a frame's worth of LCD work. C6
   bytes keep arriving meanwhile: the interrupt below buffers them. */
#define LINK_SEND_TIMEOUT_US 50000u
#define REPORT_PERIOD_MS     10000u

/* ---- UART1 receive ring, filled from the interrupt ----
 * The 32-byte hardware FIFO fills in 2.8 ms at 115200, less than one slow
 * link send or USB write. Free-running counters: head - tail is the fill. */
#define RX_RING_SIZE 1024u   /* a power of two, so the counters may wrap */
static uint8_t           s_rx_ring[RX_RING_SIZE];
static volatile uint32_t s_rx_head;       /* written only by the ISR  */
static volatile uint32_t s_rx_tail;       /* written only by the loop */
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

static uint32_t c6_uart_init(void) {
    const uint32_t actual = uart_init(C6_UART, C6_BAUD);
    uart_set_format(C6_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(C6_UART, false, false);
    uart_set_fifo_enabled(C6_UART, true);
    gpio_set_function(PIN_IO_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_IO_UART_RX, GPIO_FUNC_UART);
    irq_set_exclusive_handler(C6_UART_IRQ, on_c6_rx);
    irq_set_enabled(C6_UART_IRQ, true);
    uart_set_irq_enables(C6_UART, true, false);
    return actual;
}

/* No flow control on this UART, so this finishes in at most ~11 ms. */
static void c6_send(const char *s, size_t len) {
    uart_write_blocking(C6_UART, (const uint8_t *)s, len);
    uart_putc_raw(C6_UART, '\n');
}

/* ---- state ---- */

static bool s_link_up;      /* the inter-CPU link was brought up */
static bool s_verbose;      /* also echo the C6's once-a-second TIME lines */

static struct {
    uint32_t bytes;
    uint32_t lines;
    uint32_t link_drops;
    uint32_t last_line_ms;
} s_stats;

/* ---- C6 -> USB console and display ---- */

static void c6_line(const char *s, size_t len) {
    /* A line of nothing but replaced bytes is line noise -- the C6 puts one
       out when it brings its UART up -- not something it said. */
    size_t junk = 0u;
    while (junk < len && s[junk] == '?') junk++;
    if (junk == len) return;

    s_stats.lines++;
    s_stats.last_line_ms = to_ms_since_boot(get_absolute_time());
    const bool tick = len >= 5u && memcmp(s, "TIME ", 5u) == 0;
    if (s_verbose || !tick) DIAG("[c6] %s\n", s);
    if (s_link_up && !bn_link_send(BN_MSG_LINE, s, len, LINK_SEND_TIMEOUT_US)) {
        s_stats.link_drops++;
    }
}

/* Assemble lines from raw bytes. CR and LF both end a line. ANSI escape
   sequences (ESP-IDF colours its logs) are stripped, NULs (a break while
   the C6 resets) dropped, and anything outside the LCD font becomes '?'. */
static void c6_feed(uint8_t c) {
    static char   line[BN_TEXT_MAX + 1u];
    static size_t len;
    static enum { TXT, ESC, CSI } mode;

    s_stats.bytes++;
    if (mode == ESC) { mode = (c == '[') ? CSI : TXT; return; }
    if (mode == CSI) { if (c >= 0x40u && c <= 0x7Eu) mode = TXT; return; }
    if (c == 0x1Bu) { mode = ESC; return; }
    if (c == '\r' || c == '\n') {
        if (len > 0u) {
            line[len] = '\0';
            c6_line(line, len);
            len = 0u;
        }
        return;
    }
    if (c == '\0') return;
    if (c == '\t') c = ' ';
    if (c < 0x20u || c > 0x7Eu) c = '?';
    line[len++] = (char)c;
    if (len == BN_TEXT_MAX) {
        line[len] = '\0';
        c6_line(line, len);
        len = 0u;
    }
}

/* ---- USB console -> C6 ---- */

static void print_stats(void) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (s_stats.lines == 0u) {
        DIAG("[bottlenose_main] nothing from the C6 yet (%lu bytes). Press "
             "RESET on the Bottlenose: its boot banner should appear here.\n",
             (unsigned long)s_stats.bytes);
    } else {
        DIAG("[bottlenose_main] c6: %lu lines, %lu bytes, last %lu s ago\n",
             (unsigned long)s_stats.lines, (unsigned long)s_stats.bytes,
             (unsigned long)((now - s_stats.last_line_ms) / 1000u));
    }
    if (s_rx_overflow != 0u || s_stats.link_drops != 0u) {
        DIAG("[bottlenose_main] lost: %lu bytes (rx ring full), %lu lines "
             "(display not draining the link)\n",
             (unsigned long)s_rx_overflow, (unsigned long)s_stats.link_drops);
    }
}

static void usb_line(const char *s, size_t len) {
    if (strcmp(s, "!help") == 0) {
        DIAG("[bottlenose_main] lines you type go to the C6 (try SCAN, FETCH, "
             "STATUS, HELP). Bridge commands: !verbose (show TIME lines), "
             "!stats\n");
    } else if (strcmp(s, "!verbose") == 0) {
        s_verbose = !s_verbose;
        DIAG("[bottlenose_main] TIME lines %s\n", s_verbose ? "shown" : "hidden");
    } else if (strcmp(s, "!stats") == 0) {
        print_stats();
    } else {
        DIAG("[you] %s\n", s);
        c6_send(s, len);
    }
}

static void usb_poll(void) {
    static char   line[BN_TEXT_MAX + 1u];
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
        } else if (c >= 0x20 && c <= 0x7E && len < BN_TEXT_MAX) {
            line[len++] = (char)c;
        }
    }
}

/* ---- display -> C6 ---- */

/* ~4.2 KB (fwog_link_rx_t holds a whole max-size frame): file scope, never
   on the 2 KB stack. */
static fwog_link_rx_t s_link_rx;

static void link_poll(void) {
    uint8_t b;
    while (fwog_link_uart_read(&b)) {
        size_t len = 0u;
        if (!fwog_link_rx_byte(&s_link_rx, b, &len)) continue;
        if (len < 2u || s_link_rx.buf[0] != BN_MSG_CMD) continue;
        char cmd[BN_TEXT_MAX + 1u];
        size_t n = len - 1u;
        if (n > BN_TEXT_MAX) n = BN_TEXT_MAX;
        memcpy(cmd, &s_link_rx.buf[1], n);
        cmd[n] = '\0';
        DIAG("[display] %s\n", cmd);
        c6_send(cmd, n);
    }
}

int main(void) {
    board_init();

    /* Releases the display CPU, pushes the embedded display image if it
       differs from what the display has, then starts it. */
    const fwog_display_result_t d = fwog_display_update_run();
    /* fwog_display_update_run() brings the link up before it can fail in
       any other way, so only these two leave UART0 unconfigured. */
    s_link_up = d != FWOG_DISP_LINK_DOWN && d != FWOG_DISP_IMAGE_BAD;
    fwog_link_rx_init(&s_link_rx);

    const uint32_t baud = c6_uart_init();
    /* The C6 booted alongside this CPU and has probably already said hello
       to nobody. Ask it to repeat its state. */
    c6_send("STATUS", 6u);

    /* USB CDC is not up during the first second or two, and output written
       before a host connects is dropped -- so repeat the banner for a while. */
    const absolute_time_t announce_until = make_timeout_time_ms(10000);
    absolute_time_t next_report = make_timeout_time_ms(1000);
    while (true) {
        board_watchdog_kick();      /* every iteration, or the board resets */

        uint8_t c;
        while (c6_rx_pop(&c)) c6_feed(c);
        usb_poll();
        if (s_link_up) link_poll();

        if (time_reached(next_report)) {
            if (!time_reached(announce_until)) {
                next_report = make_timeout_time_ms(1000);
                DIAG("[bottlenose_main] display: %s; C6 on UART1 GPIO 8/9 at "
                     "%lu baud. Type !help.\n",
                     fwog_display_result_text(d), (unsigned long)baud);
            } else {
                next_report = make_timeout_time_ms(REPORT_PERIOD_MS);
                if (s_stats.lines == 0u || s_rx_overflow != 0u ||
                    s_stats.link_drops != 0u) {
                    print_stats();
                }
            }
        }
    }
}
