/* rootclock -- main CPU: OrcaBridge. See bridge.h. */
#include "bridge.h"

#include <string.h>

/* CRC-32/IEEE, the same as the BSP's fwog_crc32() and the C6 sketch's, so
 * this file needs no BSP header. */
static uint32_t crc32_ieee(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0u; i < len; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return crc ^ 0xFFFFFFFFu;
}

static void c6_say(rc_bridge_t *b, const char *s) {
    b->io.c6_line(s, strlen(s));
}

static void set_wifi_want(rc_bridge_t *b, bool on) {
    if (b->wifi_want == (on ? 1 : 0)) return;
    b->wifi_want = on ? 1 : 0;
    c6_say(b, on ? "WIFI ON" : "WIFI OFF");
}

void rc_bridge_init(rc_bridge_t *b, const rc_bridge_io_t *io, uint32_t now_ms) {
    memset(b, 0, sizeof *b);
    b->io = *io;
    rc_reasm_init(&b->rx);
    b->wifi_want = -1;
    b->st.wifi = RC_WIFI_UNKNOWN;
    b->ping_ms = now_ms;
    /* The C6 booted with the board and may have spoken to nobody. */
    c6_say(b, "STATUS");
    set_wifi_want(b, false);
}

static void start_stream(rc_bridge_t *b, uint32_t now_ms) {
    (void)now_ms;
    b->stream = RC_STREAM_BEGIN;
    b->line = 0u;
    b->st.res = RC_RES_TO_C6;
    b->st.detail = RC_DETAIL_NONE;
    b->st.http = 0u;
    b->st.tag = rc_id_tag(b->id);
}

static void finish(rc_bridge_t *b, rc_res_t res, rc_detail_t detail, uint16_t http) {
    b->stream = RC_STREAM_IDLE;
    b->st.res = res;
    b->st.detail = detail;
    b->st.http = http;
    rc_reasm_init(&b->rx);
}

bool rc_bridge_on_link(rc_bridge_t *b, const uint8_t *p, size_t n, uint32_t now_ms) {
    if (n < 1u) return false;
    if (p[0] == RC_MSG_WIFI && n >= 2u) {
        set_wifi_want(b, p[1] != 0u);
        return true;
    }
    uint8_t seq;
    bool last;
    const char *data;
    size_t dn;
    if (!rc_chunk_decode(p, n, &seq, &last, &data, &dn)) return false;

    uint8_t ack[4];
    rc_ack_t code;
    if (b->stream != RC_STREAM_IDLE && seq == 0u) {
        code = RC_ACK_BUSY;   /* still handing the previous one to the C6 */
    } else {
        code = rc_reasm_feed(&b->rx, seq, last, data, dn);
        if (code == RC_ACK_OK && seq == 0u) {
            b->st.res = RC_RES_RECEIVING;
            b->st.detail = RC_DETAIL_NONE;
            b->st.http = 0u;
        }
    }
    const size_t an = rc_ack_encode(ack, sizeof ack, seq, code);
    (void)b->io.link_send(ack, an);

    if (code == RC_ACK_OK && last && b->rx.complete && b->stream == RC_STREAM_IDLE) {
        if (!rc_json_game_id(b->rx.buf, b->rx.len, b->id)) {
            finish(b, RC_RES_FAILED, RC_DETAIL_NONE, 0u);
            return true;
        }
        b->st.tag = rc_id_tag(b->id);
        if (!b->c6_heard) {
            /* Nothing on UART1 since boot, despite STATUS pings: no C6. */
            finish(b, RC_RES_FAILED, RC_DETAIL_NO_C6, 0u);
            return true;
        }
        b->crc = crc32_ieee(b->rx.buf, b->rx.len);
        b->tries = 0u;
        set_wifi_want(b, true);   /* sending implies the radio should be on */
        start_stream(b, now_ms);
    }
    return true;
}

void rc_bridge_on_c6_line(rc_bridge_t *b, const char *line, uint32_t now_ms) {
    b->c6_heard = true;
    b->c6_last_ms = now_ms;
    b->lines_in++;
    rc_c6_line_t l;
    rc_c6_parse(line, &l);
    switch (l.kind) {
    case RC_C6_WIFI:
        b->st.wifi = l.wifi;
        b->st.wifi_why = l.wifi == RC_WIFI_DOWN ? l.why : RC_WHY_NONE;
        break;
    case RC_C6_QUEUE:
        b->st.c6_queue = (uint8_t)(l.num > 255u ? 255u : l.num);
        break;
    case RC_C6_HELLO:
        /* A C6 that (re)started has forgotten WIFI OFF/ON: say it again. */
        if (b->wifi_want >= 0) {
            const int8_t want = b->wifi_want;
            b->wifi_want = -1;
            set_wifi_want(b, want == 1);
        }
        break;
    case RC_C6_RESULT_QUEUED:
    case RC_C6_RESULT_OK:
    case RC_C6_RESULT_ERR: {
        const bool ours = b->stream != RC_STREAM_IDLE && strcmp(l.id, b->id) == 0;
        if (!ours) {
            /* About an older result the C6 had saved: report it as such. */
            b->st.tag = rc_id_tag(l.id);
            if (l.kind == RC_C6_RESULT_OK) {
                b->st.res = RC_RES_DELIVERED;
                b->st.http = (uint16_t)l.num;
            } else if (l.kind == RC_C6_RESULT_ERR && l.num >= 400u) {
                b->st.res = RC_RES_REJECTED;
                b->st.http = (uint16_t)l.num;
            } else if (l.kind == RC_C6_RESULT_QUEUED) {
                b->st.res = RC_RES_SAVED;
            }
            break;
        }
        if (l.kind == RC_C6_RESULT_QUEUED) {
            finish(b, RC_RES_SAVED, RC_DETAIL_NONE, 0u);
        } else if (l.kind == RC_C6_RESULT_OK) {
            finish(b, RC_RES_DELIVERED, RC_DETAIL_NONE, (uint16_t)l.num);
        } else if (l.num >= 400u) {
            finish(b, RC_RES_REJECTED, RC_DETAIL_NONE, (uint16_t)l.num);
        } else if (strcmp(l.reason, "nourl") == 0) {
            finish(b, RC_RES_FAILED, RC_DETAIL_NO_URL, 0u);
        } else if (++b->tries < RC_BRIDGE_C6_TRIES) {
            start_stream(b, now_ms);   /* crc / len: send it again */
        } else {
            finish(b, RC_RES_FAILED, RC_DETAIL_CRC, 0u);
        }
        break;
    }
    default:
        break;
    }
}

static bool same_status(const rc_uplink_status_t *a, const rc_uplink_status_t *b) {
    return a->c6_alive == b->c6_alive && a->wifi == b->wifi &&
           a->wifi_why == b->wifi_why && a->res == b->res && a->detail == b->detail &&
           a->http == b->http && a->c6_queue == b->c6_queue && a->tag == b->tag;
}

static void send_status(rc_bridge_t *b, uint32_t now_ms) {
    b->st.c6_alive = b->c6_heard && now_ms - b->c6_last_ms < RC_BRIDGE_C6_ALIVE_MS;
    const bool changed = !b->st_ever_sent || !same_status(&b->st, &b->st_sent);
    if (!changed && now_ms - b->st_sent_ms < RC_BRIDGE_STATUS_MS) return;
    uint8_t buf[RC_STATUS_LEN];
    const size_t n = rc_status_encode(&b->st, buf, sizeof buf);
    if (b->io.link_send(buf, n)) {
        b->st_sent = b->st;
        b->st_ever_sent = true;
        b->st_sent_ms = now_ms;
    } else {
        b->status_drops++;
        b->st_sent_ms = now_ms;   /* try again in a second, not every poll */
    }
}

void rc_bridge_poll(rc_bridge_t *b, uint32_t now_ms) {
    char line[RC_C6_LINE_MAX + 8u];
    size_t n = 0u;
    switch (b->stream) {
    case RC_STREAM_BEGIN:
        n = rc_c6_begin(line, sizeof line, b->id, b->rx.len, b->crc);
        b->stream = RC_STREAM_DATA;
        break;
    case RC_STREAM_DATA:
        n = rc_c6_data(line, sizeof line, b->rx.buf, b->rx.len, b->line++);
        if (b->line >= rc_c6_data_lines(b->rx.len)) b->stream = RC_STREAM_END;
        break;
    case RC_STREAM_END:
        n = rc_c6_end(line, sizeof line, b->id);
        b->stream = RC_STREAM_WAIT;
        b->end_ms = now_ms;
        break;
    case RC_STREAM_WAIT:
        if (now_ms - b->end_ms >= RC_BRIDGE_REPLY_MS) {
            const bool alive = b->c6_heard && now_ms - b->c6_last_ms < RC_BRIDGE_REPLY_MS;
            finish(b, RC_RES_FAILED, alive ? RC_DETAIL_TIMEOUT : RC_DETAIL_NO_C6, 0u);
        }
        break;
    default:
        break;
    }
    if (n) b->io.c6_line(line, n);

    /* A C6 that has not spoken for a while: ask it. */
    const bool quiet = !b->c6_heard || now_ms - b->c6_last_ms >= RC_BRIDGE_C6_PING_MS;
    if (quiet && now_ms - b->ping_ms >= RC_BRIDGE_C6_PING_MS) {
        b->ping_ms = now_ms;
        c6_say(b, "STATUS");
    }
    send_status(b, now_ms);
}
