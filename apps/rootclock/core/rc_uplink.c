/* rootclock -- Uplink. See rc_uplink.h for the message formats. */
#include "rc_uplink.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- small helpers ---- */

static size_t put_str(char *out, size_t cap, size_t pos, const char *s) {
    if (cap == 0u) return 0u;
    while (s && *s && pos + 1u < cap) out[pos++] = *s++;
    out[pos < cap ? pos : cap - 1u] = '\0';
    return pos;
}

static void put_u32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* ---- 0x52 status ---- */

size_t rc_status_encode(const rc_uplink_status_t *s, uint8_t *out, size_t cap) {
    if (cap < RC_STATUS_LEN) return 0u;
    out[0] = RC_MSG_UPLINK_STATUS;
    out[1] = s->c6_alive ? 1u : 0u;
    out[2] = (uint8_t)s->wifi;
    out[3] = (uint8_t)s->wifi_why;
    out[4] = (uint8_t)s->res;
    out[5] = (uint8_t)s->detail;
    out[6] = (uint8_t)s->http;
    out[7] = (uint8_t)(s->http >> 8);
    out[8] = s->c6_queue;
    put_u32le(&out[9], s->tag);
    out[13] = 0u;   /* reserved */
    return RC_STATUS_LEN;
}

bool rc_status_decode(const uint8_t *p, size_t n, rc_uplink_status_t *s) {
    if (n < RC_STATUS_LEN || p[0] != RC_MSG_UPLINK_STATUS) return false;
    if (p[2] > RC_WIFI_DOWN || p[3] > RC_WHY_OTHER || p[4] > RC_RES_FAILED ||
        p[5] > RC_DETAIL_TIMEOUT) {
        return false;
    }
    s->c6_alive = p[1] != 0u;
    s->wifi = (rc_wifi_t)p[2];
    s->wifi_why = (rc_wifi_why_t)p[3];
    s->res = (rc_res_t)p[4];
    s->detail = (rc_detail_t)p[5];
    s->http = (uint16_t)(p[6] | (p[7] << 8));
    s->c6_queue = p[8];
    s->tag = get_u32le(&p[9]);
    return true;
}

/* ---- 0x50 / 0x51 ---- */

size_t rc_chunk_encode(uint8_t *out, size_t cap, uint8_t seq, bool last,
                       const char *data, size_t n) {
    if (n > RC_CHUNK_DATA_MAX || cap < RC_CHUNK_HDR + n) return 0u;
    out[0] = RC_MSG_RESULT_CHUNK;
    out[1] = seq;
    out[2] = last ? 1u : 0u;
    memcpy(&out[RC_CHUNK_HDR], data, n);
    return RC_CHUNK_HDR + n;
}

bool rc_chunk_decode(const uint8_t *p, size_t n, uint8_t *seq, bool *last,
                     const char **data, size_t *dn) {
    if (n < RC_CHUNK_HDR || p[0] != RC_MSG_RESULT_CHUNK ||
        n - RC_CHUNK_HDR > RC_CHUNK_DATA_MAX) {
        return false;
    }
    *seq = p[1];
    *last = (p[2] & 1u) != 0u;
    *data = (const char *)&p[RC_CHUNK_HDR];
    *dn = n - RC_CHUNK_HDR;
    return true;
}

size_t rc_ack_encode(uint8_t *out, size_t cap, uint8_t seq, rc_ack_t code) {
    if (cap < 3u) return 0u;
    out[0] = RC_MSG_RESULT_ACK;
    out[1] = seq;
    out[2] = (uint8_t)code;
    return 3u;
}

bool rc_ack_decode(const uint8_t *p, size_t n, uint8_t *seq, rc_ack_t *code) {
    if (n < 3u || p[0] != RC_MSG_RESULT_ACK || p[2] > RC_ACK_BUSY) return false;
    *seq = p[1];
    *code = (rc_ack_t)p[2];
    return true;
}

/* ---- game id ---- */

bool rc_json_game_id(const char *json, size_t n, char *out) {
    static const char key[] = "\"game_id\"";
    const size_t klen = sizeof key - 1u;
    out[0] = '\0';
    for (size_t i = 0u; i + klen <= n; i++) {
        if (memcmp(json + i, key, klen) != 0) continue;
        size_t j = i + klen;
        while (j < n && (json[j] == ' ' || json[j] == ':')) j++;
        if (j >= n || json[j] != '"') return false;
        j++;
        size_t k = 0u;
        while (j < n && json[j] != '"') {
            const char c = json[j++];
            if (c == '\\' || (unsigned char)c < 0x21u || (unsigned char)c > 0x7Eu ||
                k >= RC_ID_MAX) {
                out[0] = '\0';
                return false;   /* escapes, spaces or too long: not ours */
            }
            out[k++] = c;
        }
        if (j >= n || k == 0u) {
            out[0] = '\0';
            return false;
        }
        out[k] = '\0';
        return true;
    }
    return false;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

uint32_t rc_id_tag(const char *id) {
    uint32_t v = 0u;
    unsigned i = 0u;
    for (; i < 8u && id[i]; i++) {
        const int h = hexval(id[i]);
        if (h < 0) break;
        v = (v << 4) | (uint32_t)h;
    }
    if (i == 8u) return v;
    uint32_t h = 2166136261u;   /* FNV-1a */
    for (const char *p = id; *p; p++) {
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    return h;
}

/* ---- reassembly ---- */

void rc_reasm_init(rc_reasm_t *r) {
    r->len = 0u;
    r->next_seq = 0u;
    r->active = false;
    r->complete = false;
    r->buf[0] = '\0';
}

rc_ack_t rc_reasm_feed(rc_reasm_t *r, uint8_t seq, bool last, const char *data,
                       size_t n) {
    if (seq == 0u) {
        rc_reasm_init(r);
        r->active = true;
    } else if ((r->active || r->complete) && (uint8_t)(seq + 1u) == r->next_seq) {
        return RC_ACK_OK;   /* a repeat of the chunk stored last: its ACK was lost */
    } else if (!r->active || seq != r->next_seq) {
        return RC_ACK_BAD_SEQ;
    }
    if (r->len + n > RC_UPLINK_JSON_MAX) {
        rc_reasm_init(r);
        return RC_ACK_TOO_BIG;
    }
    memcpy(r->buf + r->len, data, n);
    r->len += n;
    r->next_seq = (uint8_t)(seq + 1u);
    if (last) {
        r->buf[r->len] = '\0';
        r->complete = true;
        r->active = false;
    }
    return RC_ACK_OK;
}

/* ---- C6 lines ---- */

unsigned rc_c6_data_lines(size_t len) {
    return (unsigned)((len + RC_C6_SLICE - 1u) / RC_C6_SLICE);
}

size_t rc_c6_begin(char *out, size_t cap, const char *id, size_t bytes, uint32_t crc) {
    const int n = snprintf(out, cap, "RESULT BEGIN %s %lu %08lx", id,
                           (unsigned long)bytes, (unsigned long)crc);
    return (n < 0 || (size_t)n >= cap) ? 0u : (size_t)n;
}

size_t rc_c6_data(char *out, size_t cap, const char *json, size_t len, unsigned seq) {
    const size_t off = (size_t)seq * RC_C6_SLICE;
    if (off >= len) return 0u;
    size_t take = len - off;
    if (take > RC_C6_SLICE) take = RC_C6_SLICE;
    const int n = snprintf(out, cap, "RESULT DATA %u %.*s", seq, (int)take, json + off);
    return (n < 0 || (size_t)n >= cap) ? 0u : (size_t)n;
}

size_t rc_c6_end(char *out, size_t cap, const char *id) {
    const int n = snprintf(out, cap, "RESULT END %s", id);
    return (n < 0 || (size_t)n >= cap) ? 0u : (size_t)n;
}

/* Copy the next space-separated word of *p into out (cap bytes); advance. */
static void next_word(const char **p, char *out, size_t cap) {
    const char *s = *p;
    while (*s == ' ') s++;
    size_t k = 0u;
    while (*s && *s != ' ') {
        if (k + 1u < cap) out[k++] = *s;
        s++;
    }
    if (cap) out[k] = '\0';
    *p = s;
}

static bool starts(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

void rc_c6_parse(const char *line, rc_c6_line_t *out) {
    memset(out, 0, sizeof *out);
    out->kind = RC_C6_OTHER;
    if (!line) return;
    if (starts(line, "HELLO ")) {
        out->kind = RC_C6_HELLO;
    } else if (starts(line, "QUEUE ")) {
        out->kind = RC_C6_QUEUE;
        char w[12];
        const char *p = line + 6;
        next_word(&p, w, sizeof w);
        out->num = (unsigned)strtoul(w, NULL, 10);
    } else if (starts(line, "WIFI ")) {
        const char *v = line + 5;
        out->kind = RC_C6_WIFI;
        if (starts(v, "up")) out->wifi = RC_WIFI_UP;
        else if (starts(v, "connecting")) out->wifi = RC_WIFI_CONNECTING;
        else if (starts(v, "off")) out->wifi = RC_WIFI_OFF;
        else if (starts(v, "down")) {
            out->wifi = RC_WIFI_DOWN;
            out->why = strstr(v, "not found")  ? RC_WHY_NOT_FOUND
                     : strstr(v, "password")   ? RC_WHY_PASSWORD
                     : strstr(v, "security")   ? RC_WHY_SECURITY
                     : strstr(v, "lost")       ? RC_WHY_LOST
                                               : RC_WHY_OTHER;
        } else {
            out->kind = RC_C6_OTHER;
        }
    } else if (starts(line, "RESULT ")) {
        const char *p = line + 7;
        char verb[10];
        next_word(&p, verb, sizeof verb);
        next_word(&p, out->id, sizeof out->id);
        char rest[16];
        next_word(&p, rest, sizeof rest);
        if (out->id[0] == '\0') return;
        if (strcmp(verb, "OK") == 0) {
            out->kind = RC_C6_RESULT_OK;
            out->num = (unsigned)strtoul(rest, NULL, 10);
        } else if (strcmp(verb, "QUEUED") == 0) {
            out->kind = RC_C6_RESULT_QUEUED;
            memcpy(out->reason, rest, sizeof out->reason);
        } else if (strcmp(verb, "ERR") == 0) {
            out->kind = RC_C6_RESULT_ERR;
            memcpy(out->reason, rest, sizeof out->reason);
            out->num = (unsigned)strtoul(rest, NULL, 10);   /* 0 if a word */
        }
    }
}

/* ---- outbox ---- */

void rc_outbox_init(rc_outbox_t *ob) {
    ob->head = 0u;
    ob->count = 0u;
    ob->dropped = 0u;
}

bool rc_outbox_push(rc_outbox_t *ob, const char *json, size_t len) {
    char id[RC_ID_MAX + 1u];
    if (len == 0u || len > RC_UPLINK_JSON_MAX || !rc_json_game_id(json, len, id)) {
        return false;
    }
    for (uint8_t i = 0u; i < ob->count; i++) {
        if (strcmp(ob->item[(ob->head + i) % RC_OUTBOX_MAX].id, id) == 0) return true;
    }
    if (ob->count == RC_OUTBOX_MAX) {   /* full: the oldest goes */
        ob->head = (uint8_t)((ob->head + 1u) % RC_OUTBOX_MAX);
        ob->count--;
        ob->dropped++;
    }
    rc_outbox_item_t *it = &ob->item[(ob->head + ob->count) % RC_OUTBOX_MAX];
    memcpy(it->json, json, len);
    it->json[len] = '\0';
    it->len = (uint16_t)len;
    memcpy(it->id, id, sizeof it->id);
    it->tag = rc_id_tag(id);
    ob->count++;
    return true;
}

const rc_outbox_item_t *rc_outbox_peek(const rc_outbox_t *ob) {
    return ob->count ? &ob->item[ob->head] : NULL;
}

void rc_outbox_pop(rc_outbox_t *ob) {
    if (!ob->count) return;
    ob->head = (uint8_t)((ob->head + 1u) % RC_OUTBOX_MAX);
    ob->count--;
}

/* ---- sender ---- */

void rc_sender_init(rc_sender_t *s) { memset(s, 0, sizeof *s); }

void rc_sender_start(rc_sender_t *s, const rc_outbox_item_t *item, uint32_t now_ms) {
    rc_sender_init(s);
    if (!item) return;
    s->phase = RC_SEND_CHUNKS;
    s->tag = item->tag;
    s->sent_ms = now_ms;
}

static void sender_fail(rc_sender_t *s, rc_sendfail_t why) {
    s->phase = RC_SEND_FAILED;
    s->why = why;
    s->awaiting = false;
}

size_t rc_sender_poll(rc_sender_t *s, const rc_outbox_item_t *item,
                      uint32_t now_ms, uint8_t *out, size_t cap) {
    if (!item) return 0u;
    if (s->phase == RC_SEND_WAIT) {
        if (now_ms - s->wait_since_ms >= RC_SEND_SAVE_MS) {
            sender_fail(s, RC_SENDFAIL_UPLINK);
        }
        return 0u;
    }
    if (s->phase != RC_SEND_CHUNKS) return 0u;
    if (s->awaiting) {
        if (now_ms - s->sent_ms < RC_SEND_ACK_MS) return 0u;
        if (++s->tries >= RC_SEND_TRIES) {
            sender_fail(s, RC_SENDFAIL_NO_MAIN);
            return 0u;
        }
        /* fall through: send the same chunk again */
    }
    size_t n = item->len - s->off;
    if (n > RC_CHUNK_DATA_MAX) n = RC_CHUNK_DATA_MAX;
    const bool last = s->off + n >= item->len;
    const size_t len = rc_chunk_encode(out, cap, s->seq, last, item->json + s->off, n);
    if (len == 0u) {
        sender_fail(s, RC_SENDFAIL_REFUSED);
        return 0u;
    }
    s->chunk_len = n;
    s->awaiting = true;
    s->sent_ms = now_ms;
    return len;
}

void rc_sender_on_ack(rc_sender_t *s, const rc_outbox_item_t *item,
                      uint8_t seq, rc_ack_t code, uint32_t now_ms) {
    if (s->phase != RC_SEND_CHUNKS || !s->awaiting || !item || seq != s->seq) return;
    s->awaiting = false;
    switch (code) {
    case RC_ACK_OK:
        s->off += s->chunk_len;
        s->tries = 0u;
        if (s->off >= item->len) {
            s->phase = RC_SEND_WAIT;
            s->wait_since_ms = now_ms;
        } else {
            s->seq++;
        }
        break;
    case RC_ACK_BAD_SEQ:
        if (++s->tries >= RC_SEND_TRIES) {
            sender_fail(s, RC_SENDFAIL_REFUSED);
        } else {
            s->off = 0u;
            s->seq = 0u;
        }
        break;
    case RC_ACK_BUSY:
        /* main is still handing over the previous one: retry this chunk
           after the ACK timeout, counted as a try */
        s->awaiting = true;
        break;
    default:
        sender_fail(s, RC_SENDFAIL_REFUSED);
        break;
    }
}

void rc_sender_on_status(rc_sender_t *s, const rc_uplink_status_t *st) {
    if (s->phase != RC_SEND_WAIT || st->tag != s->tag) return;
    switch (st->res) {
    case RC_RES_SAVED:
    case RC_RES_DELIVERED:
        s->phase = RC_SEND_SAVED;
        break;
    case RC_RES_REJECTED:
    case RC_RES_FAILED:
        sender_fail(s, RC_SENDFAIL_UPLINK);
        break;
    default:
        break;
    }
}

/* ---- the GAME OVER line ---- */

static const char *wifi_why_text(rc_wifi_why_t w) {
    switch (w) {
    case RC_WHY_NOT_FOUND: return "network not found";
    case RC_WHY_PASSWORD:  return "wrong password?";
    case RC_WHY_SECURITY:  return "network needs a login";
    case RC_WHY_LOST:      return "signal lost";
    default:               return "no connection";
    }
}

static size_t put_uint(char *out, size_t cap, size_t pos, unsigned v) {
    char tmp[12];
    snprintf(tmp, sizeof tmp, "%u", v);
    return put_str(out, cap, pos, tmp);
}

rc_tone_t rc_uplink_text(const rc_uplink_view_t *v, char *out, size_t cap) {
    size_t n = 0u;
    if (cap) out[0] = '\0';
    const rc_uplink_status_t *st = &v->st;
    const rc_sender_t *sd = v->sender;

    if (!v->link_up || (sd && sd->phase == RC_SEND_FAILED && sd->why == RC_SENDFAIL_NO_MAIN)) {
        put_str(out, cap, 0u, "Upload: no answer from the main CPU");
        return RC_TONE_BAD;
    }
    if (!v->main_heard) {
        put_str(out, cap, 0u, v->outbox ? "Upload: waiting for the main CPU..." : "");
        return RC_TONE_BUSY;
    }

    /* Handing over now, or just failed to. */
    if (sd && (sd->phase == RC_SEND_CHUNKS || sd->phase == RC_SEND_WAIT)) {
        put_str(out, cap, 0u, "Saving to the WiFi module...");
        return RC_TONE_BUSY;
    }
    if (sd && sd->phase == RC_SEND_FAILED && st->tag == sd->tag) {
        if (st->res == RC_RES_REJECTED) {
            if (st->http == 401u || st->http == 403u) {
                put_str(out, cap, 0u, "Upload refused: wrong token in secrets.h");
            } else {
                n = put_str(out, cap, 0u, "Upload refused by the server (HTTP ");
                n = put_uint(out, cap, n, st->http);
                put_str(out, cap, n, ")");
            }
            return RC_TONE_BAD;
        }
        if (st->res == RC_RES_FAILED) {
            switch (st->detail) {
            case RC_DETAIL_NO_C6:  put_str(out, cap, 0u, "No WiFi module answering. Yellow: retry"); break;
            case RC_DETAIL_NO_URL: put_str(out, cap, 0u, "WiFi module has no RESULTS_URL (secrets.h)"); break;
            case RC_DETAIL_CRC:    put_str(out, cap, 0u, "WiFi module got it garbled. Yellow: retry"); break;
            default:               put_str(out, cap, 0u, "WiFi module did not confirm. Yellow: retry"); break;
            }
            return RC_TONE_BAD;
        }
    }
    if (sd && sd->phase == RC_SEND_FAILED) {
        put_str(out, cap, 0u, "Not saved to the WiFi module. Yellow: retry");
        return RC_TONE_BAD;
    }

    if (!st->c6_alive) {
        put_str(out, cap, 0u, v->outbox ? "No WiFi module found. Result kept here"
                                        : "No WiFi module found");
        return v->outbox ? RC_TONE_BAD : RC_TONE_INFO;
    }

    /* Results still on the display: say what pressing yellow will do. */
    if (v->outbox) {
        switch (st->wifi) {
        case RC_WIFI_UP:         n = put_str(out, cap, 0u, "WiFi up. Yellow: send"); break;
        case RC_WIFI_CONNECTING: n = put_str(out, cap, 0u, "WiFi connecting... Yellow: send"); break;
        case RC_WIFI_DOWN:
            n = put_str(out, cap, 0u, "WiFi: ");
            n = put_str(out, cap, n, wifi_why_text(st->wifi_why));
            n = put_str(out, cap, n, ". Yellow: save");
            break;
        default:                 n = put_str(out, cap, 0u, "WiFi off. Yellow: send"); break;
        }
        if (v->outbox > 1u) {
            n = put_str(out, cap, n, " ");
            n = put_uint(out, cap, n, v->outbox);
            put_str(out, cap, n, " games");
        }
        return st->wifi == RC_WIFI_DOWN ? RC_TONE_BAD : RC_TONE_INFO;
    }

    /* Everything handed over: report the C6's side. */
    if (st->res == RC_RES_DELIVERED && st->c6_queue == 0u) {
        put_str(out, cap, 0u, "Uploaded to root-metrics");
        return RC_TONE_GOOD;
    }
    if (st->res == RC_RES_REJECTED) {
        if (st->http == 401u || st->http == 403u) {
            put_str(out, cap, 0u, "Upload refused: wrong token in secrets.h");
        } else {
            n = put_str(out, cap, 0u, "Upload refused by the server (HTTP ");
            n = put_uint(out, cap, n, st->http);
            put_str(out, cap, n, ")");
        }
        return RC_TONE_BAD;
    }
    if (st->c6_queue > 0u || st->res == RC_RES_SAVED) {
        const unsigned q = st->c6_queue ? st->c6_queue : 1u;
        n = put_uint(out, cap, 0u, q);
        n = put_str(out, cap, n, q == 1u ? " game saved on WiFi module, " : " games saved on WiFi module, ");
        if (st->wifi == RC_WIFI_UP) {
            put_str(out, cap, n, "uploading");
            return RC_TONE_BUSY;
        }
        put_str(out, cap, n, st->wifi == RC_WIFI_DOWN ? "no WiFi" : "WiFi off");
        return RC_TONE_BUSY;
    }
    return RC_TONE_INFO;
}
