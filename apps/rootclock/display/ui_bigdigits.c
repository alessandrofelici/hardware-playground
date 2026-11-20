/* rootclock -- BigDigits: timer text above the BSP's scale-3 text cap.
 * Contract and flicker rules: ui_bigdigits.h. */
#include "ui_bigdigits.h"

#include <string.h>

#include "fwog_display.h"

/* One glyph at the largest scale: 6*8 * 8*8 = 3072 px = 6 KB of .bss.
 * Static, never a stack local: the default stack is 2 KB. Safe as a single
 * buffer because st7789_blit() is synchronous (it has finished reading the
 * pixels when it returns) and this is only reached from the main loop -- no
 * second core, no IRQ, no reentrancy. Same idiom as lcd_text.c's own glyph
 * buffer. */
#define BIG_GLYPH_MAX_PX (LCD_GLYPH_W * LCD_GLYPH_H * \
                          UI_BIGDIGITS_MAX_SCALE * UI_BIGDIGITS_MAX_SCALE)
static uint16_t s_glyph[BIG_GLYPH_MAX_PX];

typedef struct {
    int32_t x, y, w, h;
} big_rect_t;

static int32_t imax(int32_t a, int32_t b) { return a > b ? a : b; }
static int32_t imin(int32_t a, int32_t b) { return a < b ? a : b; }

static void fill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) {
    if (w <= 0 || h <= 0 || x < 0 || y < 0) return;
    /* st7789_fill_rect() clips to the panel. */
    st7789_fill_rect((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, c);
}

/* Fill the part of `a` that `b` does not cover. The end state is identical to
 * "fill all of a, then paint b", without the black flash over the overlap. */
static void fill_uncovered(big_rect_t a, big_rect_t b, uint16_t c) {
    const int32_t ix0 = imax(a.x, b.x);
    const int32_t ix1 = imin(a.x + a.w, b.x + b.w);
    const int32_t iy0 = imax(a.y, b.y);
    const int32_t iy1 = imin(a.y + a.h, b.y + b.h);
    if (ix0 >= ix1 || iy0 >= iy1) {           /* disjoint (or b empty) */
        fill(a.x, a.y, a.w, a.h, c);
        return;
    }
    fill(a.x, a.y, a.w, iy0 - a.y, c);                  /* above b  */
    fill(a.x, iy1, a.w, a.y + a.h - iy1, c);            /* below b  */
    fill(a.x, iy0, ix0 - a.x, iy1 - iy0, c);            /* left of  */
    fill(ix1, iy0, a.x + a.w - ix1, iy1 - iy0, c);      /* right of */
}

/* The font's ':' is one ink column at column 0 of its 6-column cell, while a
 * digit's ink is columns 0..3. Drawn as-is, "01:23" has a 2-column gap
 * before the colon and a 5-column gap after it. Shifting the colon's pixels
 * 1.5 columns right inside its own cell centres it between the digits; the
 * cell (and so every position and extent above) is unchanged. */
static void centre_colon(unsigned scale, uint16_t bg) {
    const unsigned w = LCD_GLYPH_W * scale;
    const unsigned h = LCD_GLYPH_H * scale;
    const unsigned k = (3u * scale) / 2u;
    for (unsigned r = 0u; r < h; r++) {
        uint16_t *row = &s_glyph[r * w];
        memmove(&row[k], &row[0], (size_t)(w - k) * sizeof row[0]);
        for (unsigned i = 0u; i < k; i++) row[i] = bg;
    }
}

/* Caller guarantees the cell lies fully on the panel. */
static void blit_glyph(uint16_t x, uint16_t y, char c, unsigned scale,
                       uint16_t fg, uint16_t bg) {
    const unsigned n = lcd_font_expand(c, scale, fg, bg, s_glyph);
    if (c == ':') centre_colon(scale, bg);
    st7789_set_window(x, y, (uint16_t)(LCD_GLYPH_W * scale),
                      (uint16_t)(LCD_GLYPH_H * scale));
    st7789_blit(s_glyph, n);
}

void ui_bigdigits_reset(ui_bigdigits_t *d) {
    if (!d) return;
    memset(d, 0, sizeof *d);
}

void ui_bigdigits_draw(ui_bigdigits_t *d, uint16_t centre_x, uint16_t y,
                       const char *str, uint8_t scale, uint16_t fg,
                       uint16_t bg) {
    if (!d) return;
    if (scale < 1u) scale = 1u;
    if (scale > UI_BIGDIGITS_MAX_SCALE) scale = UI_BIGDIGITS_MAX_SCALE;

    char s[UI_BIGDIGITS_MAX_LEN + 1u];
    uint8_t len = 0u;
    if (str) {
        while (len < UI_BIGDIGITS_MAX_LEN && str[len] != '\0') {
            s[len] = str[len];
            len++;
        }
    }
    s[len] = '\0';

    const uint32_t adv = (uint32_t)LCD_GLYPH_W * scale;
    const uint32_t h = (uint32_t)LCD_GLYPH_H * scale;
    const uint32_t width = adv * len;
    const uint32_t half = width / 2u;
    const uint16_t x = (uint16_t)(centre_x > half ? centre_x - half : 0u);

    /* Glyphs that fit entirely on the panel; the rest are dropped, never
       wrapped or partially drawn (a window past the edge would wrap). */
    uint32_t fit = 0u;
    if ((uint32_t)y + h <= ST7789_H && x < ST7789_W) {
        fit = ((uint32_t)ST7789_W - x) / adv;
        if (fit > len) fit = len;
    }

    const bool same_frame = d->valid && d->x == x && d->y == y &&
                            d->scale == scale && d->len == len &&
                            d->fg == fg && d->bg == bg;
    if (same_frame) {
        for (uint32_t i = 0u; i < fit; i++) {
            if (s[i] != d->last[i]) {
                blit_glyph((uint16_t)(x + i * adv), y, s[i], scale, fg, bg);
            }
        }
    } else {
        if (d->valid) {
            const big_rect_t old_r = {
                (int32_t)d->x, (int32_t)d->y,
                (int32_t)((uint32_t)LCD_GLYPH_W * d->scale * d->len),
                (int32_t)((uint32_t)LCD_GLYPH_H * d->scale)};
            const big_rect_t new_r = {(int32_t)x, (int32_t)y,
                                      (int32_t)(adv * fit), (int32_t)h};
            fill_uncovered(old_r, new_r, d->bg);
        }
        for (uint32_t i = 0u; i < fit; i++) {
            blit_glyph((uint16_t)(x + i * adv), y, s[i], scale, fg, bg);
        }
    }

    d->valid = true;
    d->x = x;
    d->y = y;
    d->scale = scale;
    d->len = len;
    d->fg = fg;
    d->bg = bg;
    memcpy(d->last, s, (size_t)len + 1u);
}
