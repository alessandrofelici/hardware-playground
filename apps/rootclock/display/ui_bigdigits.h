/* rootclock -- BigDigits: timer text larger than the BSP's text scale cap.
 *
 * lcd_text_draw() draws nothing above scale 3. This renders the same 6x8
 * BSP font at any scale 1..8 by expanding each glyph with lcd_font_expand()
 * into one static buffer (6*8*8*8 = 3072 px = 6 KB at scale 8) and blitting
 * it with st7789_set_window() + st7789_blit(). Each glyph cell is
 * (LCD_GLYPH_W * scale) x (LCD_GLYPH_H * scale) px; at scale 8 that is
 * 48x64, so "MM:SS" is 240 px wide and "H:MM:SS" at scale 6 is 252 px.
 *
 * Flicker control: the panel has no framebuffer and its TE pin is unused, so
 * this never clears the region when it can avoid it. With position, scale,
 * length and colours unchanged since the last call, only glyphs whose
 * character changed are re-blitted. Otherwise the previous extent is erased
 * (filled with the previous bg) and the whole string drawn. */
#ifndef UI_BIGDIGITS_H
#define UI_BIGDIGITS_H
#include <stdbool.h>
#include <stdint.h>

#define UI_BIGDIGITS_MAX_SCALE 8u
#define UI_BIGDIGITS_MAX_LEN   11u

typedef struct {
    bool     valid;                            /* false: next draw repaints   */
    uint16_t x, y;                             /* top-left of the last string */
    uint8_t  scale;
    uint8_t  len;
    uint16_t fg, bg;
    char     last[UI_BIGDIGITS_MAX_LEN + 1u];
} ui_bigdigits_t;

/* Forget the cache: the next draw repaints everything and erases nothing
 * (use after the caller has cleared the screen). */
void ui_bigdigits_reset(ui_bigdigits_t *d);

/* Draw `str` (truncated to UI_BIGDIGITS_MAX_LEN chars) horizontally centred
 * on centre_x with its top edge at y. scale is clamped to 1..8. */
void ui_bigdigits_draw(ui_bigdigits_t *d, uint16_t centre_x, uint16_t y,
                       const char *str, uint8_t scale, uint16_t fg,
                       uint16_t bg);

#endif
