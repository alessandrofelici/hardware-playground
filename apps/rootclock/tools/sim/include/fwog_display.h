/* rootclock simulator -- a stand-in for the BSP's fwog_display.h.
 *
 * Declares exactly the BSP calls the rootclock display app makes, with the
 * BSP's own signatures, so display/main.c and display/ui_*.c compile for the
 * PC unchanged. tools/sim/sim_bsp.c implements them: the LCD is a 320x240
 * framebuffer, the buttons run the BSP's real debouncer (buttons.c built with
 * HOST_TEST) and the USB console is stdin/stdout or a pseudo-terminal.
 *
 * Only what rootclock uses is here. A new BSP call in the app is a link
 * error in the simulator until it is added to this file and sim_bsp.c. */
#ifndef ROOTCLOCK_SIM_FWOG_DISPLAY_H
#define ROOTCLOCK_SIM_FWOG_DISPLAY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hardware/pio.h"

/* ---- platform/board.h ---- */
void board_init(void);
void board_backlight(uint8_t level);

/* ---- common/diag.h: the USB console ---- */
void sim_diag(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define DIAG(...) sim_diag(__VA_ARGS__)

/* ---- input/buttons.h ---- */
typedef enum {
    FWOG_BTN_GRAY = 0,
    FWOG_BTN_YELLOW,
    FWOG_BTN_GREEN,
    FWOG_BTN_BLUE,
    FWOG_BTN_RED,
    FWOG_BTN_COUNT
} fwog_btn_id_t;

typedef struct {
    uint8_t down;
    uint8_t pressed;
    uint8_t released;
} fwog_buttons_t;

void fwog_buttons_inject(uint8_t mask, uint8_t level);
void fwog_buttons_inject_clear(void);

/* ---- power/power_poll.h ---- */
typedef struct {
    fwog_buttons_t buttons;
    unsigned       progress;
    bool           armed;
} fwog_power_t;

fwog_power_t fwog_power_poll(uint32_t now_ms);
extern const unsigned char fwog_power_policy_declared;
#define FWOG_POWER_DEFAULT() \
    const unsigned char fwog_power_policy_declared = 1

/* ---- lcd/st7789.h ---- */
#define ST7789_W 320u
#define ST7789_H 240u
void st7789_init_begin(void);
void st7789_init_step(void);
bool st7789_ready(void);
void st7789_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void st7789_blit(const uint16_t *px, size_t n);
void st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      uint16_t color);
void st7789_clear(uint16_t color);

/* ---- lcd/lcd_text.h (the font half is the BSP's real lcd_text.c) ---- */
#define LCD_GLYPH_W 6u
#define LCD_GLYPH_H 8u
const uint8_t *lcd_font_glyph(char c);
unsigned lcd_text_cols(unsigned width_px, unsigned scale);
unsigned lcd_text_width_px(unsigned nchars, unsigned scale);
unsigned lcd_font_expand(char c, unsigned scale, uint16_t fg, uint16_t bg,
                         uint16_t *out);
void lcd_text_draw(uint16_t x, uint16_t y, const char *str, unsigned scale,
                   uint16_t fg, uint16_t bg);
void lcd_text_draw_padded(uint16_t x, uint16_t y, const char *str,
                          unsigned cols, unsigned scale, uint16_t fg,
                          uint16_t bg);

/* ---- power/bq25896.h: the telemetry reads rootclock makes ---- */
typedef enum {
    BQ25896_VBUS_NO_INPUT = 0,
    BQ25896_VBUS_USB_HOST = 1,
    BQ25896_VBUS_ADAPTER  = 2,
    BQ25896_VBUS_OTG      = 7
} bq25896_vbus_stat_t;
typedef enum {
    BQ25896_CHG_NOT_CHARGING = 0,
    BQ25896_CHG_PRE_CHARGE   = 1,
    BQ25896_CHG_FAST_CHARGE  = 2,
    BQ25896_CHG_DONE         = 3
} bq25896_chg_stat_t;
typedef enum {
    BQ25896_VSYS_NOT_IN_VSYSMIN = 0,
    BQ25896_VSYS_IN_VSYSMIN     = 1
} bq25896_vsys_stat_t;
typedef struct {
    bq25896_vbus_stat_t vbus_stat;
    bq25896_chg_stat_t  chg_stat;
    bq25896_vsys_stat_t vsys_stat;
} bq25896_stat_t;
bool bq25896_adc_continuous_enable(void);
bool bq25896_read_stat(bq25896_stat_t *out);
bool bq25896_read_vbat_mv(uint16_t *mv, bool *thermal_regulation);
bool bq25896_read_vbus_mv(uint16_t *mv, bool *attached);

/* ---- common/link: the inter-CPU link (framing is the BSP's real
 * link_frame.c; sim_link.c wires the UART to main's bridge) ---- */
#include "common/link/link_frame.h"
#ifndef FWOG_LINK_BAUD
#define FWOG_LINK_BAUD 12500000u
#endif
bool fwog_link_uart_init(uint32_t baud);
bool fwog_link_uart_read(uint8_t *b);
bool fwog_ioexp_link_handle(const void *payload, size_t len);

/* ---- leds/ws2812_driver.h ---- */
bool ws2812_init(PIO pio, uint sm);

#endif
