/* rootclock -- SettingsScreen: S2. Contract and layout: ui_settings.h and
 * documents/02 (S2 pixel table).
 *
 * Called every main-loop iteration while S2 is visible. Each row and the
 * preview keep what they last drew; with nothing changed a call formats ten
 * short strings, compares them and touches no SPI. Only `force` clears the
 * screen. */
#include "ui_settings.h"

#include <string.h>

#include "fwog_display.h"
#include "faction_icons.h"
#include "rc_catalog.h"
#include "rc_format.h"
#include "ui_palette.h"

/* ---- Layout (documents/02, S2) ---- */
#define SCR_W          320u

/* Title bar 0,0,320,24 on UI_BAR_BG, "SETTINGS" scale 2 at 4,4. */
#define BAR_H          24u
#define TITLE_X        4u
#define TITLE_Y        4u

/* Row list 0,28,204,190: 10 rows, 19 px pitch, scale 2 (12x16 px cells),
 * so the last row ends at y 218, just above the hint row. 204 px is exactly
 * 17 columns from x 0. The text band is y+2..y+18; the 2 px above and 1 px
 * below are painted separately so the selected row's highlight spans the
 * whole 19 px. A row is "<mark><label> <value>" with a one-character mark
 * (' ' or '>') -- the longest, ">P2 <Lizard Cult>", is exactly 17 columns,
 * which a two-character "> " mark would overflow. */
#define ROW_X          0u
#define ROW_Y0         28u
#define ROW_PITCH      19u
#define ROW_W          204u
#define ROW_COLS       17u
#define ROW_TEXT_DY    2u
#define ROW_TEXT_H     16u

/* Preview 212,28,104,190: icon 96x96 at 216,40; nickname scale 2 under it,
 * 8 columns (96 px) at 216,144, centred in those columns. For the LIMIT and
 * PLAYERS rows, three scale-1 lines of PREV_TEXT_COLS (16 cols = 96 px) at
 * 216,40/52/64 -- inside the icon rect, so drawing an icon later covers them
 * without a clear, and each line is padded so one text replaces the other. */
#define PREV_ICON_X    216u
#define PREV_ICON_Y    40u
#define PREV_NICK_X    216u
#define PREV_NICK_Y    144u
#define PREV_NICK_COLS 8u
#define PREV_TEXT_X    216u
#define PREV_TEXT_Y    40u
#define PREV_TEXT_DY   12u
#define PREV_TEXT_COLS 16u
#define PREV_TEXT_LINES 3u

/* Hint row 0,220,320,20 on UI_BAR_BG -- same geometry as S1's. */
#define HINT_Y         220u
#define HINT_H         20u
#define HINT_MARGIN    4u
#define HINT_SW        12u
#define HINT_LABEL_GAP 4u
#define HINT_ITEM_GAP  12u

/* ---- Small helpers ---- */

static size_t str_append(char *buf, size_t pos, size_t cap, const char *s) {
    if (s) {
        while (*s != '\0' && pos + 1u < cap) buf[pos++] = *s++;
    }
    buf[pos] = '\0';
    return pos;
}

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

static void draw_icon(uint16_t x, uint16_t y, uint8_t faction_id) {
    const rc_icon_t *ic = rc_icon_get(faction_id);
    if (!ic || !ic->px || ic->w == 0u || ic->h == 0u || ic->w > RC_ICON_W ||
        ic->h > RC_ICON_H) {
        st7789_fill_rect(x, y, RC_ICON_W, RC_ICON_H, UI_TILE);
        return;
    }
    if (ic->w != RC_ICON_W || ic->h != RC_ICON_H) {
        /* Not a generated icon size: tile it, then centre it. */
        st7789_fill_rect(x, y, RC_ICON_W, RC_ICON_H, UI_TILE);
    }
    st7789_set_window((uint16_t)(x + (RC_ICON_W - ic->w) / 2u),
                      (uint16_t)(y + (RC_ICON_H - ic->h) / 2u), ic->w, ic->h);
    st7789_blit(ic->px, (size_t)ic->w * ic->h);
}

/* Static, so drawn only on a repaint. All five labels fit at scale 2:
 * 5 * (12 + 4) + 10 chars * 12 + 4 gaps * 12 = 248 px. */
static void draw_hint(void) {
    static const struct {
        uint16_t    sw;
        const char *label;
    } k_items[] = {
        {UI_BTN_GRAY, "up"},  {UI_BTN_RED, "dn"},     {UI_BTN_YELLOW, "-"},
        {UI_BTN_BLUE, "+"},   {UI_BTN_GREEN, "done"},
    };
    const unsigned scale = 2u;
    const uint16_t sw_y = (uint16_t)(HINT_Y + (HINT_H - HINT_SW) / 2u);
    const uint16_t tx_y =
        (uint16_t)(HINT_Y + (HINT_H - LCD_GLYPH_H * scale) / 2u);
    st7789_fill_rect(0u, HINT_Y, SCR_W, HINT_H, UI_BAR_BG);
    unsigned x = HINT_MARGIN;
    for (unsigned i = 0u; i < sizeof k_items / sizeof k_items[0]; i++) {
        if (i) x += HINT_ITEM_GAP;
        st7789_fill_rect((uint16_t)x, sw_y, HINT_SW, HINT_SW, k_items[i].sw);
        x += HINT_SW + HINT_LABEL_GAP;
        lcd_text_draw((uint16_t)x, tx_y, k_items[i].label, scale, UI_FG,
                      UI_BAR_BG);
        x += lcd_text_width_px((unsigned)strlen(k_items[i].label), scale);
    }
}

/* ---- Screen state ---- */

typedef enum {
    PREV_NONE = 0,   /* nothing drawn (screen just cleared) */
    PREV_TEXT,       /* the LIMIT or PLAYERS explanation      */
    PREV_SEAT
} preview_t;

static bool         s_drawn;
static text_field_t s_rows[RC_MENU_ROWS];
static preview_t    s_prev;
static uint16_t     s_prev_faction;   /* 0xFFFF: no icon drawn */
static text_field_t s_prev_nick;
static text_field_t s_prev_text[PREV_TEXT_LINES];

static void invalidate_all(void) {
    memset(s_rows, 0, sizeof s_rows);
    s_prev = PREV_NONE;
    s_prev_faction = 0xFFFFu;
    memset(&s_prev_nick, 0, sizeof s_prev_nick);
    memset(s_prev_text, 0, sizeof s_prev_text);
}

/* The value shown on `row`, e.g. "2:00", "Jacob", "Lizard Cult". */
static const char *row_value(const rc_settings_t *s, uint8_t row,
                             char lim[RC_FMT_MAX + 1u]) {
    const uint8_t seat = rc_menu_row_seat(row);
    switch (rc_menu_row_kind(row)) {
    case RC_ROW_NICKNAME: {
        if (seat >= RC_SEATS) return "?";
        const char *nick = rc_nickname(s->seat[seat].nickname_id);
        return nick ? nick : "?";
    }
    case RC_ROW_FACTION: {
        if (seat >= RC_SEATS) return "?";
        const rc_faction_t *f = rc_faction(s->seat[seat].faction_id);
        return (f && f->short_name) ? f->short_name : "?";
    }
    case RC_ROW_PLAYERS:
        return rc_settings_players(s) == 3u ? "3" : "4";
    default:
        lim[0] = '\0';
        rc_fmt_limit(rc_settings_limit_ms(s), lim);
        lim[RC_FMT_MAX] = '\0';
        return lim;
    }
}

static void draw_row(const rc_settings_t *s, uint8_t row, bool selected) {
    char lim[RC_FMT_MAX + 1u];
    char seat_label[4] = "P?";
    const uint8_t seat = rc_menu_row_seat(row);
    const rc_row_kind_t kind = rc_menu_row_kind(row);
    const char *label = seat_label;
    if (kind == RC_ROW_PLAYERS) {
        label = "PLAYERS";
    } else if (kind == RC_ROW_LIMIT) {
        label = "LIMIT";
    } else if (seat < RC_SEATS) {
        seat_label[1] = (char)('1' + seat);
    }
    const char *value = row_value(s, row, lim);

    char buf[32];
    size_t n = str_append(buf, 0u, sizeof buf, selected ? ">" : " ");
    n = str_append(buf, n, sizeof buf, label);
    n = str_append(buf, n, sizeof buf, selected ? " <" : " ");
    n = str_append(buf, n, sizeof buf, value);
    if (selected) str_append(buf, n, sizeof buf, ">");

    /* A seat the green button skips (P4 with 3 players) is still editable,
       but drawn dim so it reads as sitting out. */
    const bool sits_out = seat < RC_SEATS && seat >= rc_settings_players(s);
    const uint16_t fg = sits_out ? UI_DIM : UI_FG;
    const uint16_t bg = selected ? UI_SEL_BG : UI_BG;
    const uint16_t y = (uint16_t)(ROW_Y0 + row * ROW_PITCH);
    text_field_t *f = &s_rows[row];
    if (!f->valid || f->bg != bg) {
        /* The bands above and below the text; the text paints the rest. */
        st7789_fill_rect(ROW_X, y, ROW_W, ROW_TEXT_DY, bg);
        st7789_fill_rect(ROW_X, (uint16_t)(y + ROW_TEXT_DY + ROW_TEXT_H), ROW_W,
                         ROW_PITCH - ROW_TEXT_DY - ROW_TEXT_H, bg);
    }
    field_draw(f, ROW_X, (uint16_t)(y + ROW_TEXT_DY), ROW_COLS, 2u, buf, fg,
               bg);
}

/* The PLAYERS row's explanation: the order the green button passes the
 * turn in, e.g. "P1>P2>P3>P1". */
static void players_lines(const rc_settings_t *s,
                          char lines[PREV_TEXT_LINES][PREV_TEXT_COLS + 1u]) {
    const unsigned n = rc_settings_players(s);
    size_t pos = str_append(lines[0], 0u, PREV_TEXT_COLS + 1u, "green: next turn");
    (void)pos;
    pos = 0u;
    for (unsigned i = 0u; i <= n; i++) {
        const char seat[4] = {'P', (char)('1' + (i < n ? i : 0u)), i < n ? '>' : '\0', '\0'};
        pos = str_append(lines[1], pos, PREV_TEXT_COLS + 1u, seat);
    }
    str_append(lines[2], 0u, PREV_TEXT_COLS + 1u, n < RC_SEATS ? "P4 sits out" : "");
}

static void draw_preview(const rc_settings_t *s, uint8_t sel_row) {
    uint8_t seat = RC_NO_SEAT;
    const rc_row_kind_t kind =
        sel_row < RC_MENU_ROWS ? rc_menu_row_kind(sel_row) : RC_ROW_LIMIT;
    if (kind == RC_ROW_NICKNAME || kind == RC_ROW_FACTION) {
        seat = rc_menu_row_seat(sel_row);
    }

    if (seat >= RC_SEATS) {
        char lines[PREV_TEXT_LINES][PREV_TEXT_COLS + 1u] = {{0}};
        if (kind == RC_ROW_PLAYERS) {
            players_lines(s, lines);
        } else {
            str_append(lines[0], 0u, sizeof lines[0], "beep + red at");
            str_append(lines[1], 0u, sizeof lines[1], "limit, keeps");
            str_append(lines[2], 0u, sizeof lines[2], "counting");
        }
        if (s_prev == PREV_SEAT) {
            st7789_fill_rect(PREV_ICON_X, PREV_ICON_Y, RC_ICON_W, RC_ICON_H,
                             UI_BG);
            st7789_fill_rect(PREV_NICK_X, PREV_NICK_Y,
                             PREV_NICK_COLS * LCD_GLYPH_W * 2u,
                             LCD_GLYPH_H * 2u, UI_BG);
        }
        if (s_prev != PREV_TEXT) {
            memset(s_prev_text, 0, sizeof s_prev_text);
            s_prev_faction = 0xFFFFu;
            memset(&s_prev_nick, 0, sizeof s_prev_nick);
        }
        for (unsigned i = 0u; i < PREV_TEXT_LINES; i++) {
            field_draw(&s_prev_text[i], PREV_TEXT_X,
                       (uint16_t)(PREV_TEXT_Y + i * PREV_TEXT_DY),
                       PREV_TEXT_COLS, 1u, lines[i], UI_FG, UI_BG);
        }
        s_prev = PREV_TEXT;
        return;
    }

    const rc_seat_cfg_t *cfg = &s->seat[seat];
    if (s_prev != PREV_SEAT || (uint16_t)cfg->faction_id != s_prev_faction) {
        /* Also covers the LIMIT/PLAYERS text, which lies inside the icon
           rect. */
        draw_icon(PREV_ICON_X, PREV_ICON_Y, cfg->faction_id);
        s_prev_faction = cfg->faction_id;
        memset(s_prev_text, 0, sizeof s_prev_text);
    }
    s_prev = PREV_SEAT;

    const char *nick = rc_nickname(cfg->nickname_id);
    if (!nick) nick = "?";
    char buf[PREV_NICK_COLS + 1u];
    size_t len = strlen(nick);
    if (len > PREV_NICK_COLS) len = PREV_NICK_COLS;
    const size_t lead = (PREV_NICK_COLS - len) / 2u;
    memset(buf, ' ', lead);
    memcpy(buf + lead, nick, len);
    buf[lead + len] = '\0';
    field_draw(&s_prev_nick, PREV_NICK_X, PREV_NICK_Y, PREV_NICK_COLS, 2u, buf,
               UI_FG, UI_BG);
}

void ui_settings_render(const rc_game_t *g, bool force) {
    if (!g) return;

    if (force || !s_drawn) {
        st7789_clear(UI_BG);
        st7789_fill_rect(0u, 0u, SCR_W, BAR_H, UI_BAR_BG);
        lcd_text_draw(TITLE_X, TITLE_Y, "SETTINGS", 2u, UI_FG, UI_BAR_BG);
        draw_hint();
        invalidate_all();
        s_drawn = true;
    }

    const uint8_t sel = g->menu.row;
    for (uint8_t row = 0u; row < RC_MENU_ROWS; row++) {
        draw_row(&g->settings, row, row == sel);
    }
    draw_preview(&g->settings, sel);
}
