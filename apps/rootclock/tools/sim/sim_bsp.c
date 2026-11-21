/* rootclock simulator -- the BSP calls the display app makes, for the PC.
 *
 *   LCD      a 320x240 RGB565 framebuffer. With SIM_FB=<file> set, it is
 *            written as a binary PPM (P6) whenever it changed, at most every
 *            50 ms, via <file>.tmp + rename so a reader never sees half.
 *   Console  stdin/stdout, or with SIM_PTY=<link> a pseudo-terminal whose
 *            slave path is symlinked at <link> (point hw_check.py --port at
 *            it). Log lines of the simulator itself go to stderr.
 *   Buttons  injected only (fwog_buttons_inject), debounced by the BSP's
 *            real fwog_btn_step(). Red held 6 s ends the simulator with
 *            exit status 3, as the board would power off.
 *   Time     CLOCK_MONOTONIC since start.
 *   Random   /dev/urandom, or a fixed sequence from SIM_SEED. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "fwog_display.h"
#include "pico/rand.h"
#include "pico/stdlib.h"
#include "sim_buttons.h"
#include "sim_hooks.h"

#define SHIP_HOLD_MS 6000u

/* ---- time ---- */
static uint64_t now_us_raw(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
static uint64_t s_t0;

absolute_time_t get_absolute_time(void) { return now_us_raw() - s_t0; }
uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000u); }
absolute_time_t make_timeout_time_ms(uint32_t ms) {
    return get_absolute_time() + (uint64_t)ms * 1000u;
}
absolute_time_t make_timeout_time_us(uint64_t us) { return get_absolute_time() + us; }
bool time_reached(absolute_time_t t) { return get_absolute_time() >= t; }

/* ---- framebuffer ---- */
static uint16_t s_fb[ST7789_H][ST7789_W];
static bool     s_dirty;
static uint64_t s_last_dump_us;
static uint16_t s_wx, s_wy, s_ww, s_wh;
static uint32_t s_wpos;

static void fb_dump(bool force) {
    const char *path = getenv("SIM_FB");
    if (!path || !*path || !s_dirty) return;
    const uint64_t now = get_absolute_time();
    if (!force && now - s_last_dump_us < 50000u) return;
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", ST7789_W, ST7789_H);
    for (unsigned y = 0; y < ST7789_H; y++) {
        for (unsigned x = 0; x < ST7789_W; x++) {
            const uint16_t c = s_fb[y][x];
            const uint8_t rgb[3] = {(uint8_t)(((c >> 11) & 0x1Fu) * 255u / 31u),
                                    (uint8_t)(((c >> 5) & 0x3Fu) * 255u / 63u),
                                    (uint8_t)((c & 0x1Fu) * 255u / 31u)};
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    rename(tmp, path);
    s_dirty = false;
    s_last_dump_us = now;
}

void st7789_init_begin(void) {}
void st7789_init_step(void) {}
bool st7789_ready(void) { return true; }

void st7789_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    s_wx = x; s_wy = y; s_ww = w; s_wh = h; s_wpos = 0u;
}

void st7789_blit(const uint16_t *px, size_t n) {
    for (size_t i = 0; i < n && s_ww && s_wh; i++, s_wpos++) {
        const uint32_t x = s_wx + s_wpos % s_ww, y = s_wy + s_wpos / s_ww;
        if (y >= (uint32_t)s_wy + s_wh) break;
        if (x < ST7789_W && y < ST7789_H) s_fb[y][x] = px[i];
    }
    s_dirty = true;
}

void st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      uint16_t color) {
    for (uint32_t yy = y; yy < (uint32_t)y + h && yy < ST7789_H; yy++) {
        for (uint32_t xx = x; xx < (uint32_t)x + w && xx < ST7789_W; xx++) {
            s_fb[yy][xx] = color;
        }
    }
    s_dirty = true;
}

void st7789_clear(uint16_t color) { st7789_fill_rect(0, 0, ST7789_W, ST7789_H, color); }

/* lcd_text_draw/_padded, with the BSP's rules: scale 1..3 only, glyphs that
 * would cross the right or bottom edge are not drawn, never wraps. */
static void draw_glyph(uint16_t x, uint16_t y, char c, unsigned scale,
                       uint16_t fg, uint16_t bg) {
    if (scale == 0u || scale > 3u) return;
    const unsigned w = LCD_GLYPH_W * scale, h = LCD_GLYPH_H * scale;
    if (x + w > ST7789_W || y + h > ST7789_H) return;
    uint16_t px[LCD_GLYPH_W * LCD_GLYPH_H * 9u];
    const unsigned n = lcd_font_expand(c, scale, fg, bg, px);
    st7789_set_window(x, y, (uint16_t)w, (uint16_t)h);
    st7789_blit(px, n);
}

void lcd_text_draw(uint16_t x, uint16_t y, const char *str, unsigned scale,
                   uint16_t fg, uint16_t bg) {
    if (!str || scale == 0u) return;
    const unsigned adv = LCD_GLYPH_W * scale;
    for (unsigned i = 0u; str[i]; i++) {
        const uint32_t gx = (uint32_t)x + i * adv;
        if (gx + adv > ST7789_W) break;
        draw_glyph((uint16_t)gx, y, str[i], scale, fg, bg);
    }
}

void lcd_text_draw_padded(uint16_t x, uint16_t y, const char *str,
                          unsigned cols, unsigned scale, uint16_t fg,
                          uint16_t bg) {
    if (scale == 0u) return;
    const unsigned adv = LCD_GLYPH_W * scale;
    bool ended = (str == NULL);
    for (unsigned i = 0u; i < cols; i++) {
        const uint32_t gx = (uint32_t)x + i * adv;
        if (gx + adv > ST7789_W) break;
        if (!ended && str[i] == '\0') ended = true;
        draw_glyph((uint16_t)gx, y, ended ? ' ' : str[i], scale, fg, bg);
    }
}

/* ---- console ---- */
static int s_in_fd = 0, s_out_fd = 1;

static void console_open(void) {
    const char *link = getenv("SIM_PTY");
    if (link && *link) {
        const int m = posix_openpt(O_RDWR | O_NOCTTY);
        if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0) {
            perror("sim: pty");
            exit(2);
        }
        const char *slave = ptsname(m);
        /* Raw on the slave side, so nothing is echoed or translated. */
        const int sfd = open(slave, O_RDWR | O_NOCTTY);
        if (sfd >= 0) {
            struct termios t;
            if (tcgetattr(sfd, &t) == 0) {
                cfmakeraw(&t);
                tcsetattr(sfd, TCSANOW, &t);
            }
            /* Kept open: with no slave open, writes to the master fail. */
        }
        unlink(link);
        if (symlink(slave, link) != 0) perror("sim: symlink");
        fprintf(stderr, "sim: console on %s (%s)\n", slave, link);
        s_in_fd = s_out_fd = m;
    }
    fcntl(s_in_fd, F_SETFL, fcntl(s_in_fd, F_GETFL) | O_NONBLOCK);
}

void sim_diag(const char *fmt, ...) {
    char buf[8192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if ((size_t)n >= sizeof buf) n = (int)sizeof buf - 1;
    for (int off = 0; off < n;) {
        const ssize_t w = write(s_out_fd, buf + off, (size_t)(n - off));
        if (w < 0) {
            if (errno == EAGAIN || errno == EINTR) { usleep(1000); continue; }
            return;
        }
        off += (int)w;
    }
}

int getchar_timeout_us(uint32_t timeout_us) {
    (void)timeout_us;
    unsigned char c;
    const ssize_t r = read(s_in_fd, &c, 1);
    if (r == 1) return c;
    if (r == 0 && s_in_fd == 0) {        /* stdin closed: stop like a power-off */
        fb_dump(true);
        exit(0);
    }
    return PICO_ERROR_TIMEOUT;
}

/* ---- buttons and the power-off hold ---- */
static uint8_t s_inject_mask, s_inject_level;

void fwog_buttons_inject(uint8_t mask, uint8_t level) {
    s_inject_mask = mask & 0x1Fu;
    s_inject_level = level & 0x1Fu;
}
void fwog_buttons_inject_clear(void) { s_inject_mask = s_inject_level = 0u; }

fwog_power_t fwog_power_poll(uint32_t now_ms) {
    const uint8_t raw = (uint8_t)(s_inject_mask & s_inject_level);
    const sim_buttons_t b = sim_buttons_step(raw, now_ms);
    fwog_power_t p = {{b.down, b.pressed, b.released}, 0u, false};
    if (b.red_held_ms > 0u) {
        p.armed = true;
        p.progress = b.red_held_ms >= SHIP_HOLD_MS ? 100u : b.red_held_ms * 100u / SHIP_HOLD_MS;
        if (b.red_held_ms >= SHIP_HOLD_MS) {
            fprintf(stderr, "sim: red held 6 s -> ship mode, board off\n");
            fb_dump(true);
            exit(3);
        }
    }
    sim_hooks_poll(now_ms);
    return p;
}

/* ---- the rest ---- */
void board_init(void) {
    s_t0 = now_us_raw();
    setvbuf(stderr, NULL, _IONBF, 0);
    sim_buttons_reset();
    console_open();
    sim_hooks_init();
}
void board_backlight(uint8_t level) { (void)level; }
bool ws2812_init(PIO pio, uint sm) { (void)pio; (void)sm; return true; }

void sleep_ms(uint32_t ms) {
    fb_dump(false);
    usleep(ms * 1000u);
}

uint64_t get_rand_64(void) {
    static uint64_t seq;
    const char *seed = getenv("SIM_SEED");
    if (seed && *seed) {
        seq = seq * 6364136223846793005ull + strtoull(seed, NULL, 0) + 1442695040888963407ull;
        return seq;
    }
    uint64_t v = 0;
    const int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        if (read(fd, &v, sizeof v) != (ssize_t)sizeof v) v = now_us_raw();
        close(fd);
    }
    return v;
}
