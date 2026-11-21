/* rootclock simulator -- the inter-CPU link, the main CPU's relay and the
 * ESP32-C6 (sim_hooks.h).
 *
 * The display's link bytes (uart_putc_raw on uart0) go through the BSP's real
 * frame decoder into main/bridge.c, the same relay the main CPU runs; the
 * bridge's replies are framed by the BSP's encoder and read back by the
 * display with fwog_link_uart_read(). The bridge's C6 side is the process
 * named by SIM_C6 (stdin = lines to the C6, stdout = lines from it), started
 * with /bin/sh -c; without SIM_C6 nothing answers, like a board with no
 * Bottlenose. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "bridge.h"
#include "fwog_display.h"
#include "hardware/uart.h"
#include "sim_hooks.h"

/* ---- display <- main: framed bytes waiting for fwog_link_uart_read ---- */
#define TO_DISPLAY_SIZE 65536u
static uint8_t  s_to_display[TO_DISPLAY_SIZE];
static uint32_t s_td_head, s_td_tail;

static bool to_display(const uint8_t *payload, size_t len) {
    uint8_t frame[FWOG_LINK_MAX_PAYLOAD + FWOG_LINK_OVERHEAD];
    const size_t n = fwog_link_encode(frame, sizeof frame, payload, len);
    if (n == 0u || s_td_head - s_td_tail + n > TO_DISPLAY_SIZE) return false;
    for (size_t i = 0; i < n; i++) s_to_display[(s_td_head++) % TO_DISPLAY_SIZE] = frame[i];
    return true;
}

bool fwog_link_uart_init(uint32_t baud) { (void)baud; return true; }

bool fwog_link_uart_read(uint8_t *b) {
    if (s_td_tail == s_td_head) return false;
    *b = s_to_display[(s_td_tail++) % TO_DISPLAY_SIZE];
    return true;
}

bool fwog_ioexp_link_handle(const void *payload, size_t len) {
    (void)payload; (void)len;
    return false;
}

/* ---- display -> main ---- */
static rc_bridge_t    s_bridge;
static fwog_link_rx_t s_main_rx;
static uint32_t       s_now;

bool uart_is_writable(uart_inst_t *u) { (void)u; return true; }

void uart_putc_raw(uart_inst_t *u, char c) {
    (void)u;
    size_t len = 0u;
    if (fwog_link_rx_byte(&s_main_rx, (uint8_t)c, &len)) {
        rc_bridge_on_link(&s_bridge, s_main_rx.buf, len, s_now);
    }
}

/* ---- main <-> C6 ---- */
static int  s_c6_in = -1, s_c6_out = -1;   /* write to / read from the C6 */
static pid_t s_c6_pid;

static void io_c6_line(const char *line, size_t len) {
    if (getenv("SIM_C6_TRACE")) fprintf(stderr, "sim: to c6: %.*s\n", (int)len, line);
    if (s_c6_in < 0) return;
    char buf[256];
    if (len > sizeof buf - 2u) len = sizeof buf - 2u;
    memcpy(buf, line, len);
    buf[len++] = '\n';
    if (write(s_c6_in, buf, len) < 0 && errno == EPIPE) {
        close(s_c6_in);
        s_c6_in = -1;
    }
}

static void c6_kill(void) {
    if (s_c6_pid > 0) kill(s_c6_pid, SIGTERM);
}

static void c6_start(const char *cmd) {
    int in[2], out[2];
    if (pipe(in) != 0 || pipe(out) != 0) { perror("sim: pipe"); return; }
    s_c6_pid = fork();
    if (s_c6_pid == 0) {
        dup2(in[0], 0);
        dup2(out[1], 1);
        close(in[1]);
        close(out[0]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    close(in[0]);
    close(out[1]);
    s_c6_in = in[1];
    s_c6_out = out[0];
    fcntl(s_c6_out, F_SETFL, fcntl(s_c6_out, F_GETFL) | O_NONBLOCK);
    signal(SIGPIPE, SIG_IGN);
    atexit(c6_kill);
    fprintf(stderr, "sim: C6 is `%s` (pid %d)\n", cmd, (int)s_c6_pid);
}

static void c6_read(uint32_t now) {
    static char   line[256];
    static size_t len;
    char buf[512];
    if (s_c6_out < 0) return;
    for (;;) {
        const ssize_t n = read(s_c6_out, buf, sizeof buf);
        if (n <= 0) return;
        for (ssize_t i = 0; i < n; i++) {
            const char c = buf[i];
            if (c == '\n' || c == '\r') {
                if (len) {
                    line[len] = '\0';
                    if (getenv("SIM_C6_TRACE")) fprintf(stderr, "sim: [c6] %s\n", line);
                    rc_bridge_on_c6_line(&s_bridge, line, now);
                    len = 0u;
                }
            } else if (len < sizeof line - 1u) {
                line[len++] = c;
            }
        }
    }
}

/* ---- hooks ---- */
void sim_hooks_init(void) {
    fwog_link_rx_init(&s_main_rx);
    const char *cmd = getenv("SIM_C6");
    if (cmd && *cmd) c6_start(cmd);
    const rc_bridge_io_t io = { io_c6_line, to_display };
    rc_bridge_init(&s_bridge, &io, 0u);
}

void sim_hooks_poll(uint32_t now_ms) {
    s_now = now_ms;
    c6_read(now_ms);
    rc_bridge_poll(&s_bridge, now_ms);
}
