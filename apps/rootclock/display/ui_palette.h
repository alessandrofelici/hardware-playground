/* rootclock -- colours, as RGB565 constants.
 *
 * UI_RGB() packs exactly like the BSP's st7789_rgb565() (r&0xF8 << 8 |
 * g&0xFC << 3 | b >> 3), but is a constant expression, so these can be used
 * in static initialisers. Pass the results straight to st7789_fill_rect(),
 * lcd_text_draw() and friends. */
#ifndef UI_PALETTE_H
#define UI_PALETTE_H
#include <stdint.h>

#define UI_RGB(r, g, b) \
    ((uint16_t)((((unsigned)(r) & 0xF8u) << 8) | \
                (((unsigned)(g) & 0xFCu) << 3) | \
                ((unsigned)(b) >> 3)))

/* Base */
#define UI_BG        UI_RGB(0, 0, 0)        /* screen background            */
#define UI_FG        UI_RGB(255, 255, 255)  /* normal text, big digits      */
#define UI_DIM       UI_RGB(128, 128, 128)  /* paused digits, secondary text*/
#define UI_OVER      UI_RGB(255, 0, 0)      /* over-limit digits            */
#define UI_BAR_BG    UI_RGB(24, 24, 24)     /* top bar / hint row backdrop  */
#define UI_SEL_BG    UI_RGB(40, 60, 100)    /* S2 selected row              */

/* State badges (S1 top bar, right) */
#define UI_BADGE_READY    UI_RGB(200, 200, 200)
#define UI_BADGE_RUNNING  UI_RGB(0, 200, 0)
#define UI_BADGE_PAUSED   UI_RGB(255, 190, 0)
#define UI_BADGE_OVER     UI_RGB(255, 0, 0)

/* Button swatches for the hint row -- same colours as apps/hello */
#define UI_BTN_GRAY    UI_RGB(200, 200, 200)
#define UI_BTN_YELLOW  UI_RGB(255, 190, 0)
#define UI_BTN_GREEN   UI_RGB(0, 200, 0)
#define UI_BTN_BLUE    UI_RGB(0, 80, 255)
#define UI_BTN_RED     UI_RGB(255, 0, 0)

/* Faction icon tile (#EDE3C8). tools/gen_faction_icons.py composites every
 * icon onto this colour, so a renderer that has no icon fills this. */
#define UI_TILE        UI_RGB(237, 227, 200)

#endif
