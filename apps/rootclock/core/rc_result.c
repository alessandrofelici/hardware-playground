/* rootclock -- GameResult and the contract v1 JSON serializer. See
 * rc_result.h and documents/04 section 2.
 *
 * The writer is a bounded append into the caller's buffer: no malloc, no
 * floating point, no printf. Any append that would not leave room for the
 * terminating NUL fails the whole serialization. */
#include "rc_result.h"

#include <string.h>

#include "rc_catalog.h"

void rc_result_build(rc_result_t *r, const rc_game_t *g, uint32_t now_ms,
                     uint64_t id_bits) {
    static const char hex[] = "0123456789abcdef";

    memset(r, 0, sizeof *r);
    for (unsigned i = 0u; i < RC_GAME_ID_HEX; i++) {
        unsigned shift = 4u * (RC_GAME_ID_HEX - 1u - i);   /* MS nibble first */
        r->game_id[i] = hex[(id_bits >> shift) & 0xFu];
    }
    r->game_id[RC_GAME_ID_HEX] = '\0';

    r->limit_ms = rc_settings_limit_ms(&g->settings);
    r->global_ms = rc_game_global_ms(g, now_ms);

    for (uint8_t seat = 0u; seat < RC_SEATS; seat++) {
        const rc_player_t *p = &g->players[seat];
        if (p->log.count == 0u) {
            continue;   /* never completed a turn: absent from the result */
        }
        const rc_seat_cfg_t *cfg = &g->settings.seat[seat];
        const rc_faction_t *f = rc_faction(cfg->faction_id);
        rc_result_player_t *out = &r->players[r->player_count++];
        out->seat = (uint8_t)(seat + 1u);
        out->nickname = rc_nickname(cfg->nickname_id);
        out->faction_key = (f != NULL) ? f->key : "?";
        out->total_ms = rc_player_total_ms(p, now_ms);
        out->turns_ms = p->log.turns_ms;
        out->turn_count = p->log.count;
        out->truncated = p->log.truncated;
    }
}

/* ---- bounded writer ---- */

typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
    bool   ok;
} rc_jw_t;

static void rc_jw_raw(rc_jw_t *w, const char *s, size_t n) {
    if (!w->ok || n >= w->cap - w->len) {   /* keep one byte for the NUL */
        w->ok = false;
        return;
    }
    memcpy(w->buf + w->len, s, n);
    w->len += n;
}

static void rc_jw_lit(rc_jw_t *w, const char *s) {
    rc_jw_raw(w, s, strlen(s));
}

static void rc_jw_u32(rc_jw_t *w, uint32_t v) {
    char tmp[10];
    size_t n = sizeof tmp;
    do {
        tmp[--n] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u);
    rc_jw_raw(w, tmp + n, sizeof tmp - n);
}

/* A JSON string value. The catalog strings never need escaping, but a
 * serializer that can emit invalid JSON is a trap, so escape anyway. */
static void rc_jw_str(rc_jw_t *w, const char *s) {
    static const char hex[] = "0123456789abcdef";
    rc_jw_raw(w, "\"", 1u);
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            char esc[2] = { '\\', (char)c };
            rc_jw_raw(w, esc, 2u);
        } else if (c < 0x20u) {
            char esc[6] = { '\\', 'u', '0', '0', hex[c >> 4], hex[c & 0xFu] };
            rc_jw_raw(w, esc, 6u);
        } else {
            rc_jw_raw(w, (const char *)&c, 1u);
        }
    }
    rc_jw_raw(w, "\"", 1u);
}

size_t rc_result_to_json(const rc_result_t *r, char *buf, size_t cap) {
    if (buf == NULL || cap == 0u) {
        return 0u;
    }
    rc_jw_t w = { buf, cap, 0u, true };

    rc_jw_lit(&w, "{\"schema\":\"rootclock.game_result\",\"schema_version\":1,"
                  "\"game_id\":");
    rc_jw_str(&w, r->game_id);
    rc_jw_lit(&w, ",\"device\":{\"app\":\"rootclock\",\"app_version\":");
    rc_jw_str(&w, RC_APP_VERSION);
    rc_jw_lit(&w, "},\"ended_at\":null,\"turn_limit_ms\":");
    if (r->limit_ms == 0u) {
        rc_jw_lit(&w, "null");
    } else {
        rc_jw_u32(&w, r->limit_ms);
    }
    rc_jw_lit(&w, ",\"global_ms\":");
    rc_jw_u32(&w, r->global_ms);
    rc_jw_lit(&w, ",\"players\":[");

    uint8_t count = (r->player_count <= RC_SEATS) ? r->player_count
                                                  : (uint8_t)RC_SEATS;
    for (uint8_t i = 0u; i < count; i++) {
        const rc_result_player_t *p = &r->players[i];
        if (i > 0u) {
            rc_jw_raw(&w, ",", 1u);
        }
        rc_jw_lit(&w, "{\"seat\":");
        rc_jw_u32(&w, p->seat);
        rc_jw_lit(&w, ",\"nickname\":");
        rc_jw_str(&w, p->nickname != NULL ? p->nickname : "?");
        rc_jw_lit(&w, ",\"faction\":");
        rc_jw_str(&w, p->faction_key != NULL ? p->faction_key : "?");
        rc_jw_lit(&w, ",\"total_ms\":");
        rc_jw_u32(&w, p->total_ms);
        rc_jw_lit(&w, ",\"turns_ms\":[");
        for (uint16_t t = 0u; t < p->turn_count && p->turns_ms != NULL; t++) {
            if (t > 0u) {
                rc_jw_raw(&w, ",", 1u);
            }
            rc_jw_u32(&w, p->turns_ms[t]);
        }
        rc_jw_lit(&w, "],\"turns_truncated\":");
        rc_jw_lit(&w, p->truncated ? "true" : "false");
        rc_jw_raw(&w, "}", 1u);
    }
    rc_jw_lit(&w, "]}");

    if (!w.ok) {
        buf[0] = '\0';
        return 0u;
    }
    buf[w.len] = '\0';
    return w.len;
}
