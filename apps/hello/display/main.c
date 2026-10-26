/* hello -- display CPU.
 *
 * Title on the LCD, one swatch per front-panel button that lights while it is
 * held, and on release the LED bar and the big label take that button's
 * colour. Red held 6 s still powers the board off.
 */
#include "fwog_display.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

FWOG_POWER_DEFAULT();

typedef struct {
    const char *name;
    uint8_t r, g, b;
} button_style_t;

/* Indexed by fwog_btn_id_t. */
static const button_style_t k_buttons[FWOG_BTN_COUNT] = {
    [FWOG_BTN_GRAY]   = {"GRAY",   200, 200, 200},
    [FWOG_BTN_YELLOW] = {"YELLOW", 255, 190,   0},
    [FWOG_BTN_GREEN]  = {"GREEN",    0, 200,   0},
    [FWOG_BTN_BLUE]   = {"BLUE",     0,  80, 255},
    [FWOG_BTN_RED]    = {"RED",    255,   0,   0},
};

#define SWATCH_W 56u
#define SWATCH_H 40u
#define SWATCH_Y 168u
#define LED_DIM  4u     /* LED bar at 1/4 of the swatch colour */

static uint16_t swatch_x(unsigned i) { return (uint16_t)(8u + i * 62u); }

static uint16_t rgb(const button_style_t *s, unsigned div) {
    return st7789_rgb565(s->r / div, s->g / div, s->b / div);
}

static bool lcd_start(void) {
    st7789_init_begin();
    /* Bounded: if the panel never becomes ready, init_step() is a no-op
       forever and the display CPU has no watchdog to recover it. */
    const absolute_time_t deadline = make_timeout_time_ms(500);
    while (!st7789_ready() && !time_reached(deadline)) {
        st7789_init_step();
    }
    return st7789_ready();
}

static void draw_static_screen(void) {
    st7789_clear(0x0000u);
    lcd_text_draw(52u, 20u, "hello, FreeWili OG", 2u, 0xFFFFu, 0x0000u);
    lcd_text_draw(118u, 50u, "press a button", 1u,
                  st7789_rgb565(160, 160, 160), 0x0000u);
    for (unsigned i = 0; i < FWOG_BTN_COUNT; i++) {
        st7789_fill_rect(swatch_x(i), SWATCH_Y, SWATCH_W, SWATCH_H,
                         rgb(&k_buttons[i], 5u));
        lcd_text_draw(swatch_x(i), SWATCH_Y + SWATCH_H + 4u, k_buttons[i].name,
                      1u, 0xFFFFu, 0x0000u);
    }
    lcd_text_draw(0u, 230u, "hold red 6 s to power off", 1u,
                  st7789_rgb565(120, 120, 120), 0x0000u);
    st7789_dma_wait();
}

static void show_choice(unsigned i, bool leds_ok) {
    const button_style_t *s = &k_buttons[i];
    /* Scale 4 = 24 px per glyph; pad to 6 columns so a shorter name fully
       overwrites a longer one. */
    lcd_text_draw_padded(88u, 90u, s->name, 6u, 4u, rgb(s, 1u), 0x0000u);
    if (leds_ok) {
        for (unsigned p = 0; p < FWOG_LED_COUNT; p++) {
            ws2812_set_color(p, s->r / LED_DIM, s->g / LED_DIM, s->b / LED_DIM);
        }
        ws2812_process();
    }
}

int main(void) {
    board_init();

    const bool lcd_ok = lcd_start();
    if (lcd_ok) {
        draw_static_screen();
        board_backlight(255);
    }
    const bool leds_ok = ws2812_init(pio0, 0u);
    DIAG("[hello_display] lcd=%s leds=%s\n", lcd_ok ? "ok" : "FAILED",
         leds_ok ? "ok" : "FAILED");

    uint8_t shown = 0u;
    absolute_time_t next_beat = make_timeout_time_ms(2000);
    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        const fwog_power_t power = fwog_power_poll(now);
        const fwog_buttons_t b = power.buttons;

        for (unsigned i = 0; i < FWOG_BTN_COUNT; i++) {
            const uint8_t bit = (uint8_t)FWOG_BTN_BIT(i);
            if (b.pressed & bit) {
                DIAG("[hello_display] %s pressed\n", k_buttons[i].name);
            }
            /* Act on release: while red is held the power-off countdown owns
               the LED bar, and it restores the old colours when aborted. */
            if ((b.released & bit) && !power.armed) {
                show_choice(i, leds_ok);
            }
        }

        if (lcd_ok && b.down != shown) {
            for (unsigned i = 0; i < FWOG_BTN_COUNT; i++) {
                const uint8_t bit = (uint8_t)FWOG_BTN_BIT(i);
                if ((b.down & bit) == (shown & bit)) continue;
                st7789_fill_rect(swatch_x(i), SWATCH_Y, SWATCH_W, SWATCH_H,
                                 rgb(&k_buttons[i], (b.down & bit) ? 1u : 5u));
            }
            shown = b.down;
        }

        if (time_reached(next_beat)) {
            next_beat = make_timeout_time_ms(2000);
            DIAG("[hello_display] alive\n");
        }
        sleep_ms(2);
    }
}
