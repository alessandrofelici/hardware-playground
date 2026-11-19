/* bottlenose -- display CPU: shows what the Bottlenose WiFi Orca reports.
 *
 * The ESP32-C6 on the Bottlenose talks to the MAIN CPU, which forwards each
 * line it sends over the inter-CPU link (BN_MSG_LINE, ../common/bn_link.h).
 * This half turns the lines it knows into the status rows below and puts
 * everything else -- including the C6's own boot banner -- in a scrolling
 * log. The line protocol is defined by ../c6/bottlenose_c6/bottlenose_c6.ino.
 *
 * Buttons send commands to the C6 (BN_MSG_CMD, relayed by main):
 *   yellow  SCAN    list nearby networks
 *   green   FETCH   fetch the test URL over HTTP again
 *   blue    STATUS  ask the C6 to repeat its state
 *
 * LED bar: pixel 0 = C6 heard, 1 = Wi-Fi, 2 = internet. Green is good,
 * yellow is pending, red is failed, off is unknown. Red held 6 s still
 * powers the board off. */
#include "fwog_display.h"
#include "bn_link.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FWOG_POWER_DEFAULT();

#define BLACK 0x0000u
#define WHITE 0xFFFFu

/* Status rows: scale 2 (12 x 16 px glyphs), 26 columns across 320 px. */
#define ROW_X        4u
#define ROW_Y0       28u
#define ROW_STEP     20u
#define LABEL_COLS   5u
#define VALUE_COLS   21u
#define VALUE_X      (ROW_X + LABEL_COLS * 12u)

/* Log: scale 1 (6 x 8 px glyphs), 53 columns. */
#define SEP_Y        150u
#define LOG_X        1u
#define LOG_Y0       156u
#define LOG_STEP     10u
#define LOG_ROWS     7u
#define LOG_COLS     53u
#define FOOTER_Y     230u

#define REFRESH_MS   250u
/* Once NTP has synced the C6 sends TIME every second, but before that (no
   network, wrong password) it may only re-send its state every 30 s. */
#define STALE_MS     35000u
#define CMD_TIMEOUT_US 20000u

typedef enum { ST_UNKNOWN = 0, ST_PENDING, ST_OK, ST_FAIL } st_t;

enum { ROW_C6, ROW_WIFI, ROW_IP, ROW_RSSI, ROW_TIME, ROW_WEB, ROW_COUNT };
static const char *const k_label[ROW_COUNT] = {
    "C6", "WiFi", "IP", "RSSI", "Time", "Web",
};

/* What the C6 has told us. */
static struct {
    bool     heard;
    uint32_t heard_ms;
    char     fw[16];
    char     wifi[32];
    st_t     wifi_st;
    char     ssid[40];
    char     ip[24];
    bool     have_rssi;
    int      rssi;
    char     time[24];
    char     web[BN_TEXT_MAX + 1u];
    st_t     web_st;
} s_c6;

static bool s_link_up;
static bool s_lcd_ok;
static bool s_leds_ok;
static uint16_t c_gray, c_label, c_green, c_yellow, c_red;

/* ---- log ---- */

static char s_log[LOG_ROWS][LOG_COLS + 1u];
static bool s_log_dirty;

static void log_add(const char *s) {
    memmove(s_log[0], s_log[1], sizeof s_log[0] * (LOG_ROWS - 1u));
    strncpy(s_log[LOG_ROWS - 1u], s, LOG_COLS);
    s_log[LOG_ROWS - 1u][LOG_COLS] = '\0';
    s_log_dirty = true;
}

/* ---- parsing the C6's lines ---- */

/* True when `s` is `key` alone or `key` followed by a space; *rest is then
   what follows. */
static bool key_is(const char *s, const char *key, const char **rest) {
    const size_t n = strlen(key);
    if (strncmp(s, key, n) != 0) return false;
    if (s[n] == '\0') { *rest = s + n; return true; }
    if (s[n] != ' ')  return false;
    *rest = s + n + 1u;
    return true;
}

/* Copy and report whether anything changed. */
static bool set_str(char *dst, size_t cap, const char *src) {
    if (strncmp(dst, src, cap - 1u) == 0) return false;
    strncpy(dst, src, cap - 1u);
    dst[cap - 1u] = '\0';
    return true;
}

/* Repeated values (the C6 re-sends its state every 30 s) are logged only
   when they change; TIME and RSSI tick, so they are never logged. */
static void handle_line(const char *s, uint32_t now) {
    s_c6.heard = true;
    s_c6.heard_ms = now;

    const char *v;
    if (key_is(s, "TIME", &v)) {
        set_str(s_c6.time, sizeof s_c6.time, v);
    } else if (key_is(s, "RSSI", &v)) {
        s_c6.rssi = atoi(v);
        s_c6.have_rssi = true;
    } else if (key_is(s, "HELLO", &v)) {
        set_str(s_c6.fw, sizeof s_c6.fw, v);
        log_add(s);
    } else if (key_is(s, "WIFI", &v)) {
        if (set_str(s_c6.wifi, sizeof s_c6.wifi, v)) log_add(s);
        if (strcmp(v, "up") == 0) {
            s_c6.wifi_st = ST_OK;
        } else {
            s_c6.wifi_st = strncmp(v, "connecting", 10) == 0 ? ST_PENDING
                                                             : ST_FAIL;
            s_c6.ip[0] = '\0';
            s_c6.have_rssi = false;
        }
    } else if (key_is(s, "SSID", &v)) {
        if (set_str(s_c6.ssid, sizeof s_c6.ssid, v)) log_add(s);
    } else if (key_is(s, "IP", &v)) {
        if (set_str(s_c6.ip, sizeof s_c6.ip, v)) log_add(s);
    } else if (key_is(s, "NET", &v)) {
        s_c6.web_st = ST_OK;
        if (set_str(s_c6.web, sizeof s_c6.web, v)) log_add(s);
    } else if (key_is(s, "HTTP", &v)) {
        log_add(s);
        if (strncmp(v, "200", 3) != 0) {
            s_c6.web_st = ST_FAIL;
            set_str(s_c6.web, sizeof s_c6.web, v);
        }
    } else {
        log_add(s);
    }
}

/* ---- link ---- */

/* ~4.2 KB (a whole max-size frame): file scope, never on the stack. */
static fwog_link_rx_t s_link_rx;

static void link_poll(uint32_t now) {
    uint8_t b;
    while (fwog_link_uart_read(&b)) {
        size_t len = 0u;
        if (!fwog_link_rx_byte(&s_link_rx, b, &len)) continue;
        if (len >= 1u && s_link_rx.buf[0] == BN_MSG_LINE) {
            char line[BN_TEXT_MAX + 1u];
            size_t n = len - 1u;
            if (n > BN_TEXT_MAX) n = BN_TEXT_MAX;
            memcpy(line, &s_link_rx.buf[1], n);
            line[n] = '\0';
            handle_line(line, now);
        } else {
            /* Breakout-direction requests. This app's main never sends
               them, but servicing them costs nothing. */
            (void)fwog_ioexp_link_handle(s_link_rx.buf, len);
        }
    }
}

static void send_cmd(const char *cmd) {
    char note[24];
    snprintf(note, sizeof note, "> %s", cmd);
    log_add(note);
    if (!s_link_up ||
        !bn_link_send(BN_MSG_CMD, cmd, strlen(cmd), CMD_TIMEOUT_US)) {
        log_add("  not sent: link to main is down");
    }
}

/* ---- screen ---- */

static bool lcd_start(void) {
    st7789_init_begin();
    /* Bounded: if the panel never becomes ready, init_step() is a no-op
       forever and the display CPU has no watchdog to recover it. */
    const absolute_time_t deadline = make_timeout_time_ms(500);
    while (!st7789_ready() && !time_reached(deadline)) st7789_init_step();
    return st7789_ready();
}

static uint16_t row_y(unsigned row) {
    return (uint16_t)(ROW_Y0 + row * ROW_STEP);
}

static void draw_static_screen(void) {
    st7789_clear(BLACK);
    const char *title = "Bottlenose Wi-Fi";
    lcd_text_draw((uint16_t)((ST7789_W - lcd_text_width_px(strlen(title), 2u)) / 2u),
                  4u, title, 2u, WHITE, BLACK);
    for (unsigned i = 0; i < ROW_COUNT; i++) {
        lcd_text_draw(ROW_X, row_y(i), k_label[i], 2u, c_label, BLACK);
    }
    st7789_fill_rect(0u, SEP_Y, ST7789_W, 1u, c_gray);
    lcd_text_draw(LOG_X, FOOTER_Y,
                  "yellow scan | green fetch | blue status | red 6s off",
                  1u, c_gray, BLACK);
    st7789_dma_wait();
}

/* The value each row shows, and its colour. */
static uint16_t row_value(unsigned row, uint32_t now, char *out, size_t n) {
    const bool wifi_up = s_c6.wifi_st == ST_OK;
    switch (row) {
    case ROW_C6:
        if (!s_c6.heard) {
            snprintf(out, n, "waiting for data");
            return c_gray;
        }
        if (now - s_c6.heard_ms >= STALE_MS) {
            snprintf(out, n, "quiet for %lu s",
                     (unsigned long)((now - s_c6.heard_ms) / 1000u));
            return c_yellow;
        }
        if (s_c6.fw[0] != '\0') snprintf(out, n, "talking, fw %s", s_c6.fw);
        else                    snprintf(out, n, "talking");
        return c_green;
    case ROW_WIFI:
        switch (s_c6.wifi_st) {
        case ST_OK:      snprintf(out, n, "up  %s", s_c6.ssid);  return c_green;
        case ST_PENDING: snprintf(out, n, "joining %s", s_c6.ssid); return c_yellow;
        case ST_FAIL:    snprintf(out, n, "%s", s_c6.wifi);    return c_red;
        default:         snprintf(out, n, "-");                return c_gray;
        }
    case ROW_IP:
        if (s_c6.ip[0] == '\0') { snprintf(out, n, "-"); return c_gray; }
        snprintf(out, n, "%s", s_c6.ip);
        return c_green;
    case ROW_RSSI:
        if (!s_c6.have_rssi) { snprintf(out, n, "-"); return c_gray; }
        snprintf(out, n, "%d dBm", s_c6.rssi);
        return s_c6.rssi >= -60 ? c_green : s_c6.rssi >= -75 ? c_yellow : c_red;
    case ROW_TIME:
        if (s_c6.time[0] != '\0') { snprintf(out, n, "%s", s_c6.time); return c_green; }
        snprintf(out, n, "%s", wifi_up ? "syncing..." : "-");
        return wifi_up ? c_yellow : c_gray;
    case ROW_WEB:
        if (s_c6.web_st == ST_OK)   { snprintf(out, n, "%s", s_c6.web); return c_green; }
        if (s_c6.web_st == ST_FAIL) { snprintf(out, n, "%s", s_c6.web); return c_red; }
        snprintf(out, n, "%s", wifi_up ? "fetching..." : "-");
        return wifi_up ? c_yellow : c_gray;
    default:
        out[0] = '\0';
        return c_gray;
    }
}

/* What each row currently shows, so only changes are redrawn. */
static struct {
    bool     valid;
    uint16_t color;
    char     text[VALUE_COLS + 1u];
} s_drawn[ROW_COUNT];

static void draw_rows(uint32_t now) {
    for (unsigned i = 0; i < ROW_COUNT; i++) {
        char full[BN_TEXT_MAX + 16u];
        const uint16_t color = row_value(i, now, full, sizeof full);
        char text[VALUE_COLS + 1u];
        strncpy(text, full, VALUE_COLS);
        text[VALUE_COLS] = '\0';
        if (s_drawn[i].valid && s_drawn[i].color == color &&
            strcmp(s_drawn[i].text, text) == 0) {
            continue;
        }
        lcd_text_draw_padded(VALUE_X, row_y(i), text, VALUE_COLS, 2u, color, BLACK);
        s_drawn[i].valid = true;
        s_drawn[i].color = color;
        strcpy(s_drawn[i].text, text);
    }
}

static void draw_log(void) {
    const uint16_t fg = st7789_rgb565(210, 210, 210);
    for (unsigned i = 0; i < LOG_ROWS; i++) {
        lcd_text_draw_padded(LOG_X, (uint16_t)(LOG_Y0 + i * LOG_STEP),
                             s_log[i], LOG_COLS, 1u, fg, BLACK);
    }
}

/* ---- LED bar ---- */

static void led_color(st_t s, uint8_t *r, uint8_t *g, uint8_t *b) {
    switch (s) {
    case ST_OK:      *r = 0;  *g = 24; *b = 0; break;
    case ST_PENDING: *r = 24; *g = 16; *b = 0; break;
    case ST_FAIL:    *r = 24; *g = 0;  *b = 0; break;
    default:         *r = 0;  *g = 0;  *b = 0; break;
    }
}

static void show_leds(uint32_t now) {
    static int shown = -1;
    const st_t c6 = !s_c6.heard                     ? ST_UNKNOWN
                  : now - s_c6.heard_ms >= STALE_MS ? ST_PENDING
                  :                                   ST_OK;
    const st_t px[3] = {c6, s_c6.wifi_st, s_c6.web_st};
    const int key = (int)px[0] | ((int)px[1] << 4) | ((int)px[2] << 8);
    if (key == shown) return;
    shown = key;
    for (unsigned p = 0; p < FWOG_LED_COUNT; p++) {
        uint8_t r = 0, g = 0, b = 0;
        if (p < 3u) led_color(px[p], &r, &g, &b);
        ws2812_set_color(p, r, g, b);
    }
    ws2812_process();
}

int main(void) {
    board_init();

    /* The bootloader takes the link down before jumping here, so the app
       brings it back up itself. */
    s_link_up = fwog_link_uart_init(FWOG_LINK_BAUD);
    fwog_link_rx_init(&s_link_rx);

    c_gray   = st7789_rgb565(120, 120, 120);
    c_label  = st7789_rgb565(150, 170, 200);
    c_green  = st7789_rgb565(0, 210, 0);
    c_yellow = st7789_rgb565(255, 190, 0);
    c_red    = st7789_rgb565(255, 40, 40);

    s_lcd_ok = lcd_start();
    if (s_lcd_ok) {
        draw_static_screen();
        board_backlight(255);
    }
    s_leds_ok = ws2812_init(pio0, 0u);
    DIAG("[bottlenose_display] lcd=%s leds=%s link=%s\n",
         s_lcd_ok ? "ok" : "FAILED", s_leds_ok ? "ok" : "FAILED",
         s_link_up ? "ok" : "FAILED");

    /* Main asked the C6 for its state before this half was running, so the
       answer went nowhere. Ask again. */
    if (s_link_up) send_cmd("STATUS");
    else           log_add("link to main FAILED: nothing will arrive");

    absolute_time_t next_refresh = make_timeout_time_ms(REFRESH_MS);
    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        /* Once per iteration, and take buttons from its result -- never call
           fwog_buttons_poll() as well, the power-off hold depends on it. */
        const fwog_power_t power = fwog_power_poll(now);

        /* No sleep in this loop: main's link sends wait on this drain. */
        if (s_link_up) link_poll(now);

        const uint8_t pressed = power.buttons.pressed;
        if (pressed & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) send_cmd("SCAN");
        if (pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN))  send_cmd("FETCH");
        if (pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE))   send_cmd("STATUS");

        if (time_reached(next_refresh)) {
            next_refresh = make_timeout_time_ms(REFRESH_MS);
            if (s_lcd_ok) {
                draw_rows(now);
                if (s_log_dirty) {
                    draw_log();
                    s_log_dirty = false;
                }
            }
            /* While red is held the power-off countdown owns the LED bar,
               and it restores the old colours when aborted. */
            if (s_leds_ok && !power.armed) show_leds(now);
        }
    }
}
