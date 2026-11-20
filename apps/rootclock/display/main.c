/* rootclock -- display CPU: the whole app.
 *
 * A chess-clock-style turn timer for 3-4 players of Root. The loop is:
 *   fwog_power_poll() -> test console -> rc_input_map() -> rc_game_apply()
 *   -> rc_game_tick() -> beep / result -> render S1 or S2 -> beeper_process()
 *
 * Game logic lives in core/ (SDK-free); this file only wires it to the BSP.
 * Planning documents: documents/ at the repository root.
 *
 * ---- Test console (USB CDC, 115200; NEVER open the port at 1200 baud,
 *      that reboots this CPU into BOOTSEL) ----
 *   press <gray|yellow|green|blue|red>   inject a 100 ms tap
 *   hold <btn> <ms>                      inject a hold; red refused >= 1000 ms
 *   state                                one "STATE {json}" line
 *   result                               re-print the last "RESULT {json}"
 *   reset                                new READY game with default settings
 *   help
 * Presses go through fwog_buttons_inject(), so they run the real debounce and
 * the real power-off path -- which is exactly why red holds are capped: red
 * held 6 s powers the board off and only a human can switch it back on.
 * apps/rootclock/tools/hw_check.py drives this to run the acceptance tests
 * unattended. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fwog_display.h"
#include "hardware/pio.h"
#include "pico/rand.h"
#include "pico/stdlib.h"

#include "beeper.h"
#include "rc_game.h"
#include "rc_input.h"
#include "rc_result.h"
#include "rc_types.h"
#include "ui_clock.h"
#include "ui_palette.h"
#include "ui_settings.h"

/* Red held 6 s powers the board off, with a countdown on the LED bar. */
FWOG_POWER_DEFAULT();

/* core/ mirrors the BSP's button ids and bit order; keep them locked. */
_Static_assert((unsigned)RC_BTN_GRAY == (unsigned)FWOG_BTN_GRAY, "btn ids");
_Static_assert((unsigned)RC_BTN_YELLOW == (unsigned)FWOG_BTN_YELLOW, "btn ids");
_Static_assert((unsigned)RC_BTN_GREEN == (unsigned)FWOG_BTN_GREEN, "btn ids");
_Static_assert((unsigned)RC_BTN_BLUE == (unsigned)FWOG_BTN_BLUE, "btn ids");
_Static_assert((unsigned)RC_BTN_RED == (unsigned)FWOG_BTN_RED, "btn ids");
_Static_assert((unsigned)RC_BTN_COUNT == (unsigned)FWOG_BTN_COUNT, "btn ids");

#define TAP_MS          100u
#define RED_HOLD_MAX_MS 999u      /* hard cap: never approach the 6 s power-off */
#define HOLD_MAX_MS     10000u
#define LINE_MAX        64u

static rc_game_t   s_game;
static rc_input_t  s_input;
static rc_result_t s_result;
static char        s_json[RC_RESULT_JSON_MAX];
static bool        s_have_result;
static uint32_t    s_beeps;        /* RC_EVT_BEEP count, for the test console */
static uint32_t    s_games_over;   /* RC_EVT_GAME_OVER count                   */
static bool        s_force_redraw;

/* ---- LCD bring-up (same bounded pattern as apps/hello) ---- */
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

/* ---- Test console ---- */
typedef struct {
    bool     active;
    uint8_t  mask;
    uint32_t start_ms;
    uint32_t release_ms;
} inject_t;

static inject_t s_inject;

static const char *const k_btn_names[RC_BTN_COUNT] = {
    "gray", "yellow", "green", "blue", "red"
};

static const char *state_name(rc_state_t s) {
    switch (s) {
    case RC_STATE_READY:    return "READY";
    case RC_STATE_RUNNING:  return "RUNNING";
    case RC_STATE_PAUSED:   return "PAUSED";
    case RC_STATE_SETTINGS: return "SETTINGS";
    case RC_STATE_OVER:     return "OVER";
    }
    return "?";
}

static int btn_from_name(const char *name) {
    for (unsigned i = 0; i < RC_BTN_COUNT; i++) {
        if (name && strcmp(name, k_btn_names[i]) == 0) return (int)i;
    }
    return -1;
}

static void inject_start(unsigned btn, uint32_t ms, uint32_t now) {
    s_inject.active = true;
    s_inject.mask = RC_BTN_BIT(btn);
    s_inject.start_ms = now;
    s_inject.release_ms = now + ms;
    fwog_buttons_inject(s_inject.mask, s_inject.mask);
}

static void inject_poll(uint32_t now) {
    if (!s_inject.active) return;
    const bool red = (s_inject.mask & RC_BTN_BIT(RC_BTN_RED)) != 0u;
    /* Belt and braces for red, independent of release_ms: an injected red
       press never outlives RED_HOLD_MAX_MS, so the 6 s power-off is
       unreachable from the console. */
    if ((int32_t)(now - s_inject.release_ms) >= 0 ||
        (red && now - s_inject.start_ms >= RED_HOLD_MAX_MS)) {
        fwog_buttons_inject_clear();
        s_inject.active = false;
    }
}

static void print_state(uint32_t now) {
    const rc_game_t *g = &s_game;
    const uint8_t active = g->active_seat;
    const uint32_t turn = (active < RC_SEATS) ? rc_game_turn_ms(g, active, now) : 0u;
    uint32_t totals[RC_SEATS];
    for (uint8_t i = 0; i < RC_SEATS; i++) totals[i] = rc_game_total_ms(g, i, now);
    const uint32_t global = rc_game_global_ms(g, now);

    DIAG("STATE {\"app\":\"rootclock\",\"state\":\"%s\",\"screen\":\"%s\","
         "\"active\":%d,\"viewed\":%d,\"shown\":%u,\"turn_ms\":%lu,"
         "\"totals\":[%lu,%lu,%lu,%lu],\"global_ms\":%lu,"
         "\"turns\":[%u,%u,%u,%u],\"logged\":[%u,%u,%u,%u],"
         "\"over\":%s,\"limit_ms\":%lu,\"row\":%u,"
         "\"nick\":[%u,%u,%u,%u],\"fac\":[%u,%u,%u,%u],"
         "\"hold\":%u,\"beeps\":%lu,\"games_over\":%lu,\"inject\":%s}\n",
         state_name(g->state), g->state == RC_STATE_SETTINGS ? "S2" : "S1",
         active < RC_SEATS ? (int)active : -1,
         g->viewed_seat < RC_SEATS ? (int)g->viewed_seat : -1,
         (unsigned)rc_game_shown_seat(g), (unsigned long)turn,
         (unsigned long)totals[0], (unsigned long)totals[1],
         (unsigned long)totals[2], (unsigned long)totals[3],
         (unsigned long)global,
         (unsigned)g->players[0].turns_started, (unsigned)g->players[1].turns_started,
         (unsigned)g->players[2].turns_started, (unsigned)g->players[3].turns_started,
         (unsigned)g->players[0].log.count, (unsigned)g->players[1].log.count,
         (unsigned)g->players[2].log.count, (unsigned)g->players[3].log.count,
         rc_game_turn_over_limit(g, now) ? "true" : "false",
         (unsigned long)rc_settings_limit_ms(&g->settings), (unsigned)g->menu.row,
         (unsigned)g->settings.seat[0].nickname_id, (unsigned)g->settings.seat[1].nickname_id,
         (unsigned)g->settings.seat[2].nickname_id, (unsigned)g->settings.seat[3].nickname_id,
         (unsigned)g->settings.seat[0].faction_id, (unsigned)g->settings.seat[1].faction_id,
         (unsigned)g->settings.seat[2].faction_id, (unsigned)g->settings.seat[3].faction_id,
         rc_input_hold_progress(&s_input, g->state, now),
         (unsigned long)s_beeps, (unsigned long)s_games_over,
         s_inject.active ? "true" : "false");
}

static void game_reset(void) {
    fwog_buttons_inject_clear();
    s_inject.active = false;
    rc_game_init(&s_game);
    rc_input_init(&s_input);
    s_force_redraw = true;
}

static void console_line(char *line, uint32_t now) {
    char *argv[4] = {0};
    unsigned argc = 0;
    for (char *tok = strtok(line, " \t"); tok && argc < 4u; tok = strtok(NULL, " \t")) {
        argv[argc++] = tok;
    }
    if (argc == 0u) return;

    if (strcmp(argv[0], "press") == 0 || strcmp(argv[0], "hold") == 0) {
        const bool hold = argv[0][0] == 'h';
        const int btn = btn_from_name(argc > 1u ? argv[1] : NULL);
        if (btn < 0) { DIAG("ERR %s: button gray|yellow|green|blue|red\n", argv[0]); return; }
        uint32_t ms = TAP_MS;
        if (hold) {
            const long v = (argc > 2u) ? strtol(argv[2], NULL, 10) : -1;
            if (v <= 0 || v > (long)HOLD_MAX_MS) { DIAG("ERR hold: ms 1..%u\n", HOLD_MAX_MS); return; }
            ms = (uint32_t)v;
        }
        if (btn == (int)RC_BTN_RED && ms > RED_HOLD_MAX_MS) {
            DIAG("ERR red hold refused (max %u ms; 6 s powers the board off)\n", RED_HOLD_MAX_MS);
            return;
        }
        if (s_inject.active) { DIAG("ERR busy\n"); return; }
        inject_start((unsigned)btn, ms, now);
        DIAG("OK %s %s %lu\n", argv[0], k_btn_names[btn], (unsigned long)ms);
    } else if (strcmp(argv[0], "state") == 0) {
        print_state(now);
    } else if (strcmp(argv[0], "result") == 0) {
        if (s_have_result) DIAG("RESULT %s\n", s_json);
        else DIAG("ERR no result yet\n");
    } else if (strcmp(argv[0], "reset") == 0) {
        game_reset();
        DIAG("OK reset\n");
    } else if (strcmp(argv[0], "help") == 0) {
        DIAG("OK commands: press <btn> | hold <btn> <ms> | state | result | reset | help\n");
    } else {
        DIAG("ERR unknown command '%s'\n", argv[0]);
    }
}

static void console_poll(uint32_t now) {
    static char line[LINE_MAX];
    static unsigned len;
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\r' || c == '\n') {
            if (len > 0u) {
                line[len] = '\0';
                console_line(line, now);
                len = 0u;
            }
        } else if (len < LINE_MAX - 1u) {
            line[len++] = (char)c;
        }
    }
}

/* ---- Game over: snapshot and print the contract-v1 JSON once ---- */
static void on_game_over(uint32_t now) {
    rc_result_build(&s_result, &s_game, now, get_rand_64());
    const size_t n = rc_result_to_json(&s_result, s_json, sizeof s_json);
    s_have_result = n > 0u;
    s_games_over++;
    if (s_have_result) {
        DIAG("RESULT %s\n", s_json);
    } else {
        DIAG("ERR result did not fit in %u bytes\n", (unsigned)sizeof s_json);
    }
}

int main(void) {
    board_init();

    const bool lcd_ok = lcd_start();
    if (lcd_ok) board_backlight(255);
    /* The LED bar is the power-off countdown's display; nothing else uses it. */
    const bool leds_ok = ws2812_init(pio0, 0u);
    const bool audio_ok = beeper_init();

    game_reset();
    DIAG("[rootclock_display] lcd=%s leds=%s audio=%s\n", lcd_ok ? "ok" : "FAILED",
         leds_ok ? "ok" : "FAILED", audio_ok ? "ok" : "FAILED");

    int shown_screen = 0;   /* 0 = nothing yet, 1 = S1, 2 = S2 */
    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        /* Once per iteration, and take buttons from its result -- never call
           fwog_buttons_poll() as well, the power-off hold depends on it. */
        const fwog_power_t power = fwog_power_poll(now);

        console_poll(now);
        inject_poll(now);

        const rc_buttons_t b = { power.buttons.down, power.buttons.pressed,
                                 power.buttons.released };
        const rc_action_t a = rc_input_map(&s_input, s_game.state, b, now);
        rc_events_t ev = RC_EVT_NONE;
        if (a.kind != RC_ACT_NONE) ev |= rc_game_apply(&s_game, a, now);
        ev |= rc_game_tick(&s_game, now);

        if (ev & RC_EVT_BEEP) {
            s_beeps++;
            beeper_beep();
        }
        if (ev & RC_EVT_GAME_OVER) on_game_over(now);

        if (lcd_ok) {
            const int screen = (s_game.state == RC_STATE_SETTINGS) ? 2 : 1;
            const bool force = s_force_redraw || screen != shown_screen;
            if (screen == 1) {
                ui_clock_render(&s_game, now,
                                rc_input_hold_progress(&s_input, s_game.state, now),
                                force);
            } else {
                ui_settings_render(&s_game, force);
            }
            shown_screen = screen;
            s_force_redraw = false;
        }

        beeper_process();
        /* Poll fast: the debouncer and the turn timestamps want ~2 ms. */
        sleep_ms(2);
    }
}
