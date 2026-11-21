/* rootclock -- ClockScreen: S1. Contract and layout: ui_clock.h and
 * documents/02 (S1 pixel table).
 *
 * Called every main-loop iteration. Every field keeps the text and colours it
 * last drew; with nothing changed a call formats a few strings, compares them
 * and touches no SPI. Only `force` clears the screen.
 *
 * Text widths: the BSP font cell is 6x8, so scale 1/2/3 is 6/12/18 px per
 * character. Every field below has a fixed column count chosen so it fits
 * its region and is always drawn padded to that count, so a shorter string
 * erases a longer one. */
#include "ui_clock.h"

#include <string.h>

#include "fwog_display.h"
#include "faction_icons.h"
#include "rc_catalog.h"
#include "rc_format.h"
#include "ui_bigdigits.h"
#include "ui_palette.h"

/* ---- Layout (documents/02, S1) ---- */
#define SCR_W          320u

/* Top bar 0,0,320,24 on UI_BAR_BG; text scale 2 (16 px) at y 4. */
#define BAR_H          24u
#define BAR_TEXT_Y     4u
#define GAME_X         4u
#define GAME_COLS      15u   /* "GAME " + "HHHH:MM:SS" worst case: x 4..184 */
/* Badge: a filled pill, right-aligned to x 316, y 2..22, black scale-2 text
 * with 4 px padding. Widest is "GAME OVER": 9*12+8 = 116 px -> x 200..316,
 * so the badge area starts at 200, clear of the GAME field (ends at 184). */
#define BADGE_AREA_X   200u
#define BADGE_RIGHT    316u
#define BADGE_Y        2u
#define BADGE_H        20u
#define BADGE_PAD      4u

/* Icon tile 8,30,96,96. */
#define ICON_X         8u
#define ICON_Y         30u

/* Right column, x 116. Scale 3 fits (320-116)/18 = 11 cols (to x 314);
 * scale 2 fits (320-116)/12 = 17 cols (to x 320). */
#define COL_X          116u
#define NAME_Y         30u
#define NAME_COLS      11u
#define AVG_Y          60u
#define TOTAL_Y        82u
#define TURN_Y         104u
#define COL_COLS       17u

/* Big timer: centred on x 160, top y 140. "MM:SS" at scale 8 = 240x64
 * (x 40..280); "H:MM:SS" at scale 6 = 252x48 (x 34..286). */
#define BIG_CX         160u
#define BIG_Y          140u
#define BIG_SCALE_MMSS 8u
#define BIG_SCALE_HMS  6u

/* Hint row 0,220,320,20 on UI_BAR_BG. */
#define HINT_Y         220u
#define HINT_H         20u
#define HINT_MARGIN    4u
#define HINT_SW        12u   /* swatch 12x12, y 224..236 */
#define HINT_SW_GAP    2u    /* between swatches of one item */
#define HINT_LABEL_GAP 4u    /* swatch -> label */
#define HINT_ITEM_GAP  12u   /* label -> next item's swatch */
#define HINT_BAR_H     8u    /* hold progress bar, y 226..234 */
#define HOLD_TRACK     UI_RGB(64, 64, 64)

/* ---- Small helpers (no snprintf: cheap enough to run every iteration) ---- */

static size_t str_append(char *buf, size_t pos, size_t cap, const char *s) {
    if (s) {
        while (*s != '\0' && pos + 1u < cap) buf[pos++] = *s++;
    }
    buf[pos] = '\0';
    return pos;
}

static size_t uint_append(char *buf, size_t pos, size_t cap, unsigned v) {
    char tmp[10];
    unsigned n = 0u;
    do {
        tmp[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v != 0u && n < sizeof tmp);
    while (n > 0u && pos + 1u < cap) buf[pos++] = tmp[--n];
    buf[pos] = '\0';
    return pos;
}

/* Fill rect a minus rect b (b assumed inside the panel's coordinate range). */
static void fill_minus(int32_t ax, int32_t ay, int32_t aw, int32_t ah,
                       int32_t bx, int32_t by, int32_t bw, int32_t bh,
                       uint16_t c) {
    const int32_t ix0 = ax > bx ? ax : bx;
    const int32_t ix1 = (ax + aw) < (bx + bw) ? (ax + aw) : (bx + bw);
    const int32_t iy0 = ay > by ? ay : by;
    const int32_t iy1 = (ay + ah) < (by + bh) ? (ay + ah) : (by + bh);
    if (ix0 >= ix1 || iy0 >= iy1) {
        if (aw > 0 && ah > 0) {
            st7789_fill_rect((uint16_t)ax, (uint16_t)ay, (uint16_t)aw,
                             (uint16_t)ah, c);
        }
        return;
    }
    if (iy0 > ay) {
        st7789_fill_rect((uint16_t)ax, (uint16_t)ay, (uint16_t)aw,
                         (uint16_t)(iy0 - ay), c);
    }
    if (ay + ah > iy1) {
        st7789_fill_rect((uint16_t)ax, (uint16_t)iy1, (uint16_t)aw,
                         (uint16_t)(ay + ah - iy1), c);
    }
    if (ix0 > ax) {
        st7789_fill_rect((uint16_t)ax, (uint16_t)iy0, (uint16_t)(ix0 - ax),
                         (uint16_t)(iy1 - iy0), c);
    }
    if (ax + aw > ix1) {
        st7789_fill_rect((uint16_t)ix1, (uint16_t)iy0,
                         (uint16_t)(ax + aw - ix1), (uint16_t)(iy1 - iy0), c);
    }
}

/* ---- Cached text field ---- */

#define TF_MAX_COLS 20u

typedef struct {
    bool     valid;
    uint16_t fg, bg;
    char     text[TF_MAX_COLS + 1u];   /* as drawn, space-padded to cols */
} text_field_t;

/* Draw `s` at (x, y) padded to `cols`. A field must always be drawn with the
 * same x, y, cols and scale. Unchanged: no SPI. Same colours: only the
 * changed cells are redrawn. Otherwise the whole field, padded. */
static void field_draw(text_field_t *f, uint16_t x, uint16_t y, unsigned cols,
                       unsigned scale, const char *s, uint16_t fg,
                       uint16_t bg) {
    if (cols > TF_MAX_COLS) cols = TF_MAX_COLS;
    char want[TF_MAX_COLS + 1u];
    unsigned i = 0u;
    if (s) {
        for (; i < cols && s[i] != '\0'; i++) want[i] = s[i];
    }
    for (; i < cols; i++) want[i] = ' ';
    want[cols] = '\0';

    if (f->valid && f->fg == fg && f->bg == bg) {
        if (memcmp(want, f->text, cols) == 0) return;
        const unsigned adv = LCD_GLYPH_W * scale;
        for (i = 0u; i < cols; i++) {
            if (want[i] == f->text[i]) continue;
            const char one[2] = {want[i], '\0'};
            lcd_text_draw((uint16_t)(x + i * adv), y, one, scale, fg, bg);
        }
    } else {
        lcd_text_draw_padded(x, y, want, cols, scale, fg, bg);
    }
    memcpy(f->text, want, (size_t)cols + 1u);
    f->fg = fg;
    f->bg = bg;
    f->valid = true;
}

/* ---- Icon ---- */

static void draw_icon(uint16_t x, uint16_t y, uint8_t faction_id) {
    const rc_icon_t *ic = rc_icon_get(faction_id);
    if (!ic || !ic->px || ic->w == 0u || ic->h == 0u || ic->w > RC_ICON_W ||
        ic->h > RC_ICON_H) {
        st7789_fill_rect(x, y, RC_ICON_W, RC_ICON_H, UI_TILE);
        return;
    }
    /* A real icon is exactly 96x96; anything smaller is centred on the tile. */
    const uint16_t ox = (uint16_t)(x + (RC_ICON_W - ic->w) / 2u);
    const uint16_t oy = (uint16_t)(y + (RC_ICON_H - ic->h) / 2u);
    if (ic->w != RC_ICON_W || ic->h != RC_ICON_H) {
        fill_minus(x, y, RC_ICON_W, RC_ICON_H, ox, oy, ic->w, ic->h, UI_TILE);
    }
    st7789_set_window(ox, oy, ic->w, ic->h);
    st7789_blit(ic->px, (size_t)ic->w * ic->h);
}

/* ---- Hint row ---- */

typedef struct {
    uint8_t     nsw;      /* swatches, 1..4 */
    uint16_t    sw[4];
    const char *label;
} hint_item_t;

typedef enum {
    HINT_NONE = 0,   /* nothing drawn yet */
    HINT_READY,
    HINT_RUNNING,
    HINT_PAUSED,
    HINT_OVER,
    HINT_HOLD
} hint_mode_t;

static const hint_item_t k_hint_ready[] = {
    {1u, {UI_BTN_RED}, "start"},
    {1u, {UI_BTN_YELLOW}, "settings"},
};
static const hint_item_t k_hint_running[] = {
    {1u, {UI_BTN_GREEN}, "next"},
    {1u, {UI_BTN_RED}, "pause"},
};
static const hint_item_t k_hint_paused[] = {
    {1u, {UI_BTN_RED}, "resume"},
    {1u, {UI_BTN_GREEN}, "view"},
    {1u, {UI_BTN_YELLOW}, "settings"},
    {1u, {UI_BTN_BLUE}, "hold: end"},
};
static const hint_item_t k_hint_over[] = {
    {1u, {UI_BTN_GREEN}, "view"},
    {1u, {UI_BTN_BLUE}, "hold: new game"},
};
static const hint_item_t k_hint_hold[] = {
    {1u, {UI_BTN_BLUE}, "keep holding..."},
};

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

static unsigned hint_item_width(const hint_item_t *it, unsigned scale) {
    const unsigned nsw = it->nsw ? it->nsw : 1u;
    return nsw * HINT_SW + (nsw - 1u) * HINT_SW_GAP + HINT_LABEL_GAP +
           lcd_text_width_px((unsigned)strlen(it->label), scale);
}

static unsigned hint_width(const hint_item_t *it, unsigned n, unsigned scale) {
    unsigned w = 0u;
    for (unsigned i = 0u; i < n; i++) {
        if (i) w += HINT_ITEM_GAP;
        w += hint_item_width(&it[i], scale);
    }
    return w;
}

/* Clear the row and draw the items left to right from HINT_MARGIN. Scale 2
 * when the whole row fits in 320 - 2*4 px, else scale 1. Returns the x just
 * past the last label. */
static unsigned hint_draw(const hint_item_t *it, unsigned n) {
    st7789_fill_rect(0u, HINT_Y, SCR_W, HINT_H, UI_BAR_BG);
    const unsigned scale =
        hint_width(it, n, 2u) <= SCR_W - 2u * HINT_MARGIN ? 2u : 1u;
    const uint16_t sw_y = (uint16_t)(HINT_Y + (HINT_H - HINT_SW) / 2u);
    const uint16_t tx_y =
        (uint16_t)(HINT_Y + (HINT_H - LCD_GLYPH_H * scale) / 2u);
    unsigned x = HINT_MARGIN;
    for (unsigned i = 0u; i < n; i++) {
        if (i) x += HINT_ITEM_GAP;
        const unsigned nsw = it[i].nsw ? it[i].nsw : 1u;
        for (unsigned j = 0u; j < nsw && j < 4u; j++) {
            if (j) x += HINT_SW_GAP;
            st7789_fill_rect((uint16_t)x, sw_y, HINT_SW, HINT_SW, it[i].sw[j]);
            x += HINT_SW;
        }
        x += HINT_LABEL_GAP;
        lcd_text_draw((uint16_t)x, tx_y, it[i].label, scale, UI_FG, UI_BAR_BG);
        x += lcd_text_width_px((unsigned)strlen(it[i].label), scale);
    }
    return x;
}

/* ---- Screen state ---- */

typedef enum {
    BADGE_NONE = 0,
    BADGE_READY,
    BADGE_RUNNING,
    BADGE_PAUSED,
    BADGE_OVER
} badge_t;

static bool           s_drawn;          /* false: next call repaints all */
static text_field_t   s_game;
static badge_t        s_badge;
static uint16_t       s_icon_faction;   /* 0xFFFF: icon not drawn        */
static text_field_t   s_name;
static text_field_t   s_avg;
static text_field_t   s_total;
static text_field_t   s_turn;
static ui_bigdigits_t s_big;
static hint_mode_t    s_hint;
static uint16_t       s_hold_x0;        /* progress track, HINT_HOLD only */
static uint16_t       s_hold_w;
static uint16_t       s_hold_drawn;     /* px of the track filled blue    */

static void invalidate_all(void) {
    memset(&s_game, 0, sizeof s_game);
    s_badge = BADGE_NONE;
    s_icon_faction = 0xFFFFu;
    memset(&s_name, 0, sizeof s_name);
    memset(&s_avg, 0, sizeof s_avg);
    memset(&s_total, 0, sizeof s_total);
    memset(&s_turn, 0, sizeof s_turn);
    ui_bigdigits_reset(&s_big);
    s_hint = HINT_NONE;
    s_hold_x0 = s_hold_w = s_hold_drawn = 0u;
}

static void draw_badge(badge_t b) {
    const char *label = "READY";
    uint16_t colour = UI_BADGE_READY;
    switch (b) {
    case BADGE_RUNNING: label = "RUNNING";   colour = UI_BADGE_RUNNING; break;
    case BADGE_PAUSED:  label = "PAUSED";    colour = UI_BADGE_PAUSED;  break;
    case BADGE_OVER:    label = "GAME OVER"; colour = UI_BADGE_OVER;    break;
    default: break;
    }
    const uint16_t tw = (uint16_t)lcd_text_width_px((unsigned)strlen(label), 2u);
    const uint16_t bw = (uint16_t)(tw + 2u * BADGE_PAD);
    const uint16_t bx = (uint16_t)(BADGE_RIGHT - bw);
    /* Paint around the new badge and around its text, never under it, so a
       state change does not flash. */
    fill_minus(BADGE_AREA_X, 0, SCR_W - BADGE_AREA_X, BAR_H, bx, BADGE_Y, bw,
               BADGE_H, UI_BAR_BG);
    fill_minus(bx, BADGE_Y, bw, BADGE_H, bx + BADGE_PAD, BAR_TEXT_Y, tw,
               LCD_GLYPH_H * 2u, colour);
    lcd_text_draw((uint16_t)(bx + BADGE_PAD), BAR_TEXT_Y, label, 2u, UI_BG,
                  colour);
}

static void draw_hint(hint_mode_t mode, unsigned hold_progress) {
    if (mode != s_hint) {
        switch (mode) {
        case HINT_READY:
            hint_draw(k_hint_ready, COUNT_OF(k_hint_ready));
            break;
        case HINT_RUNNING:
            hint_draw(k_hint_running, COUNT_OF(k_hint_running));
            break;
        case HINT_PAUSED:
            hint_draw(k_hint_paused, COUNT_OF(k_hint_paused));
            break;
        case HINT_OVER:
            hint_draw(k_hint_over, COUNT_OF(k_hint_over));
            break;
        case HINT_HOLD: {
            const unsigned end = hint_draw(k_hint_hold, COUNT_OF(k_hint_hold));
            const unsigned x0 = end + HINT_ITEM_GAP;
            const unsigned x1 = SCR_W - HINT_MARGIN;
            s_hold_x0 = (uint16_t)x0;
            s_hold_w = (uint16_t)(x1 > x0 ? x1 - x0 : 0u);
            s_hold_drawn = 0u;
            if (s_hold_w) {
                st7789_fill_rect(s_hold_x0, HINT_Y + (HINT_H - HINT_BAR_H) / 2u,
                                 s_hold_w, HINT_BAR_H, HOLD_TRACK);
            }
            break;
        }
        default:
            break;
        }
        s_hint = mode;
    }
    if (mode == HINT_HOLD && s_hold_w) {
        const unsigned p = hold_progress > 100u ? 100u : hold_progress;
        const uint16_t want = (uint16_t)((unsigned)s_hold_w * p / 100u);
        const uint16_t bar_y = HINT_Y + (HINT_H - HINT_BAR_H) / 2u;
        if (want > s_hold_drawn) {
            st7789_fill_rect((uint16_t)(s_hold_x0 + s_hold_drawn), bar_y,
                             (uint16_t)(want - s_hold_drawn), HINT_BAR_H,
                             UI_BTN_BLUE);
        } else if (want < s_hold_drawn) {
            st7789_fill_rect((uint16_t)(s_hold_x0 + want), bar_y,
                             (uint16_t)(s_hold_drawn - want), HINT_BAR_H,
                             HOLD_TRACK);
        }
        s_hold_drawn = want;
    }
}

void ui_clock_render(const rc_game_t *g, uint32_t now_ms,
                     unsigned hold_progress, bool force) {
    if (!g) return;

    if (force || !s_drawn) {
        st7789_clear(UI_BG);
        st7789_fill_rect(0u, 0u, SCR_W, BAR_H, UI_BAR_BG);
        invalidate_all();
        s_drawn = true;
    }

    /* S1 is not normally visible in SETTINGS; if it is, show the state S2
       will return to. */
    rc_state_t st = g->state;
    if (st == RC_STATE_SETTINGS) {
        st = (g->return_state == RC_STATE_PAUSED) ? RC_STATE_PAUSED
                                                  : RC_STATE_READY;
    }

    uint8_t seat = rc_game_shown_seat(g);
    if (seat >= RC_SEATS) seat = 0u;
    const rc_seat_cfg_t *cfg = &g->settings.seat[seat];

    char buf[32];
    char t[RC_FMT_MAX + 1u];
    size_t n;

    /* Top bar: "GAME H:MM:SS" and the state badge. */
    t[0] = '\0';
    rc_fmt_hms(rc_game_global_ms(g, now_ms), t);
    t[RC_FMT_MAX] = '\0';
    n = str_append(buf, 0u, sizeof buf, "GAME ");
    str_append(buf, n, sizeof buf, t);
    field_draw(&s_game, GAME_X, BAR_TEXT_Y, GAME_COLS, 2u, buf, UI_FG,
               UI_BAR_BG);

    badge_t badge = BADGE_READY;
    hint_mode_t hint = HINT_READY;
    switch (st) {
    case RC_STATE_RUNNING: badge = BADGE_RUNNING; hint = HINT_RUNNING; break;
    case RC_STATE_PAUSED:  badge = BADGE_PAUSED;  hint = HINT_PAUSED;  break;
    case RC_STATE_OVER:    badge = BADGE_OVER;    hint = HINT_OVER;    break;
    default: break;
    }
    if (badge != s_badge) {
        draw_badge(badge);
        s_badge = badge;
    }

    /* Icon tile: only when the shown faction changes. */
    if ((uint16_t)cfg->faction_id != s_icon_faction) {
        draw_icon(ICON_X, ICON_Y, cfg->faction_id);
        s_icon_faction = cfg->faction_id;
    }

    /* "P<n> <nickname>", scale 3. */
    n = str_append(buf, 0u, sizeof buf, "P");
    n = uint_append(buf, n, sizeof buf, (unsigned)seat + 1u);
    n = str_append(buf, n, sizeof buf, " ");
    const char *nick = rc_nickname(cfg->nickname_id);
    str_append(buf, n, sizeof buf, nick ? nick : "?");
    field_draw(&s_name, COL_X, NAME_Y, NAME_COLS, 3u, buf, UI_FG, UI_BG);

    /* "AVG/TURN MM:SS": the average completed turn, scale 2. The current
       turn is left out, so the value only changes when a turn ends. */
    {
        uint32_t avg_ms = 0u;
        n = str_append(buf, 0u, sizeof buf, "AVG/TURN ");
        if (rc_game_avg_turn_ms(g, seat, &avg_ms)) {
            t[0] = '\0';
            rc_fmt_turn(avg_ms, t);
            t[RC_FMT_MAX] = '\0';
            str_append(buf, n, sizeof buf, t);
        } else {
            str_append(buf, n, sizeof buf, "--:--");
        }
        field_draw(&s_avg, COL_X, AVG_Y, COL_COLS, 2u, buf, UI_FG, UI_BG);
    }

    /* "TOTAL H:MM:SS", scale 2. */
    const uint32_t total_ms = rc_game_total_ms(g, seat, now_ms);
    t[0] = '\0';
    rc_fmt_hms(total_ms, t);
    t[RC_FMT_MAX] = '\0';
    n = str_append(buf, 0u, sizeof buf, "TOTAL ");
    str_append(buf, n, sizeof buf, t);
    field_draw(&s_total, COL_X, TOTAL_Y, COL_COLS, 2u, buf, UI_FG, UI_BG);

    /* "TURN <n>  LIM <m:ss|off>", scale 2, 17 cols. Falls back to one space,
       then to "T<n> LIM ..." so even 5-digit turn counts with a 5-char
       limit fit. */
    {
        char lim[RC_FMT_MAX + 1u];
        lim[0] = '\0';
        rc_fmt_limit(rc_settings_limit_ms(&g->settings), lim);
        lim[RC_FMT_MAX] = '\0';
        const unsigned turns = g->players[seat].turns_started;
        static const char *const k_fmt_turn[3] = {"TURN ", "TURN ", "T"};
        static const char *const k_fmt_sep[3] = {"  LIM ", " LIM ", " LIM "};
        for (unsigned v = 0u; v < 3u; v++) {
            n = str_append(buf, 0u, sizeof buf, k_fmt_turn[v]);
            n = uint_append(buf, n, sizeof buf, turns);
            n = str_append(buf, n, sizeof buf, k_fmt_sep[v]);
            n = str_append(buf, n, sizeof buf, lim);
            if (n <= COL_COLS) break;
        }
        field_draw(&s_turn, COL_X, TURN_Y, COL_COLS, 2u, buf, UI_FG, UI_BG);
    }

    /* Big timer. */
    uint16_t big_fg = UI_FG;
    t[0] = '\0';
    switch (st) {
    case RC_STATE_RUNNING:
        rc_fmt_turn(rc_game_turn_ms(g, seat, now_ms), t);
        big_fg = rc_game_turn_over_limit(g, now_ms) ? UI_OVER : UI_FG;
        break;
    case RC_STATE_PAUSED:
        /* The paused turn on the active seat's card; any other card (green
           cycles them) shows that seat's total instead. */
        rc_fmt_turn(seat == g->active_seat ? rc_game_turn_ms(g, seat, now_ms)
                                           : total_ms,
                    t);
        big_fg = UI_DIM;
        break;
    case RC_STATE_OVER:
        rc_fmt_turn(total_ms, t);
        break;
    default: /* READY */
        str_append(t, 0u, sizeof t, "00:00");
        break;
    }
    t[RC_FMT_MAX] = '\0';
    const uint8_t big_scale =
        strlen(t) <= 5u ? (uint8_t)BIG_SCALE_MMSS : (uint8_t)BIG_SCALE_HMS;
    ui_bigdigits_draw(&s_big, BIG_CX, BIG_Y, t, big_scale, big_fg, UI_BG);

    /* Hint row. */
    draw_hint(hold_progress > 0u ? HINT_HOLD : hint, hold_progress);
}
