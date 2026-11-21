/* rootclock host tests -- Uplink: link payloads, reassembly, C6 lines, the
 * display's outbox and sender, the GAME OVER line; and the main CPU's bridge
 * (main/bridge.c) driven end to end against a fake display and a fake C6. */
#include "rc_test_support.h"

#include "bridge.h"
#include "rc_uplink.h"

/* A contract-v1 result like the board prints, ~2.3 KB so it takes 3 chunks
 * and ~42 C6 lines. */
static char s_json[RC_UPLINK_JSON_MAX + 1u];
static size_t s_json_len;

static void make_json(const char *id, unsigned turns) {
    size_t n = (size_t)snprintf(s_json, sizeof s_json,
        "{\"schema\":\"rootclock.game_result\",\"schema_version\":1,"
        "\"game_id\":\"%s\",\"device\":{\"app\":\"rootclock\",\"app_version\":\"004\"},"
        "\"ended_at\":null,\"turn_limit_ms\":null,\"global_ms\":0,\"players\":[", id);
    for (unsigned p = 0u; p < 3u; p++) {
        n += (size_t)snprintf(s_json + n, sizeof s_json - n,
            "%s{\"seat\":%u,\"nickname\":\"Jacob\",\"faction\":\"marquise\",\"total_ms\":0,"
            "\"turns_ms\":[", p ? "," : "", p + 1u);
        for (unsigned t = 0u; t < turns; t++) {
            n += (size_t)snprintf(s_json + n, sizeof s_json - n, "%s%u", t ? "," : "",
                                  100000u + t);
        }
        n += (size_t)snprintf(s_json + n, sizeof s_json - n, "],\"turns_truncated\":false}");
    }
    n += (size_t)snprintf(s_json + n, sizeof s_json - n, "]}");
    s_json_len = n;
}

static uint32_t crc32_ref(const char *s, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint8_t)s[i];
        for (int k = 0; k < 8; k++) c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return c ^ 0xFFFFFFFFu;
}

/* ---- payload formats ---- */

static void test_status_roundtrip(void) {
    rc_uplink_status_t a = { true, RC_WIFI_DOWN, RC_WHY_PASSWORD, RC_RES_REJECTED,
                             RC_DETAIL_NONE, 401u, 7u, 0xDEADBEEFu };
    rc_uplink_status_t b;
    uint8_t buf[32];
    ASSERT_EQ(rc_status_encode(&a, buf, sizeof buf), RC_STATUS_LEN);
    ASSERT_TRUE(RC_STATUS_LEN + 5u <= 32u);   /* fits the UART FIFO, framed */
    ASSERT_TRUE(rc_status_decode(buf, RC_STATUS_LEN, &b));
    ASSERT_TRUE(b.c6_alive);
    ASSERT_EQ(b.wifi, RC_WIFI_DOWN);
    ASSERT_EQ(b.wifi_why, RC_WHY_PASSWORD);
    ASSERT_EQ(b.res, RC_RES_REJECTED);
    ASSERT_EQ(b.http, 401u);
    ASSERT_EQ(b.c6_queue, 7u);
    ASSERT_EQ(b.tag, 0xDEADBEEFu);
    buf[4] = 99u;                                /* unknown res */
    ASSERT_TRUE(!rc_status_decode(buf, RC_STATUS_LEN, &b));
    ASSERT_TRUE(!rc_status_decode(buf, 5u, &b));
}

static void test_chunk_and_ack(void) {
    uint8_t buf[RC_CHUNK_HDR + RC_CHUNK_DATA_MAX];
    ASSERT_EQ(rc_chunk_encode(buf, sizeof buf, 7u, true, "abc", 3u), 6u);
    uint8_t seq; bool last; const char *d; size_t dn;
    ASSERT_TRUE(rc_chunk_decode(buf, 6u, &seq, &last, &d, &dn));
    ASSERT_EQ(seq, 7u);
    ASSERT_TRUE(last);
    ASSERT_EQ(dn, 3u);
    ASSERT_TRUE(memcmp(d, "abc", 3u) == 0);
    ASSERT_EQ(rc_chunk_encode(buf, sizeof buf, 0u, false, s_json, RC_CHUNK_DATA_MAX + 1u), 0u);
    rc_ack_t code;
    ASSERT_EQ(rc_ack_encode(buf, sizeof buf, 9u, RC_ACK_BUSY), 3u);
    ASSERT_TRUE(rc_ack_decode(buf, 3u, &seq, &code));
    ASSERT_EQ(seq, 9u);
    ASSERT_EQ(code, RC_ACK_BUSY);
}

static void test_game_id(void) {
    char id[RC_ID_MAX + 1u];
    make_json("7f3a9c21e04b5d18", 2u);
    ASSERT_TRUE(rc_json_game_id(s_json, s_json_len, id));
    ASSERT_STREQ(id, "7f3a9c21e04b5d18");
    ASSERT_EQ(rc_id_tag(id), 0x7f3a9c21u);
    ASSERT_TRUE(rc_id_tag("not-hex-at-all") != rc_id_tag("not-hex-at-alm"));
    ASSERT_TRUE(!rc_json_game_id("{\"game_id\":\"has space\"}", 23u, id));
    ASSERT_TRUE(!rc_json_game_id("{\"x\":1}", 7u, id));
    ASSERT_TRUE(!rc_json_game_id("{\"game_id\":\"\"}", 14u, id));
}

static void test_reassembly(void) {
    static rc_reasm_t r;
    rc_reasm_init(&r);
    ASSERT_EQ(rc_reasm_feed(&r, 1u, false, "x", 1u), RC_ACK_BAD_SEQ);   /* no seq 0 */
    ASSERT_EQ(rc_reasm_feed(&r, 0u, false, "ab", 2u), RC_ACK_OK);
    ASSERT_EQ(rc_reasm_feed(&r, 0u, false, "ab", 2u), RC_ACK_OK);       /* restart */
    ASSERT_EQ(rc_reasm_feed(&r, 1u, false, "cd", 2u), RC_ACK_OK);
    ASSERT_EQ(rc_reasm_feed(&r, 1u, false, "cd", 2u), RC_ACK_OK);       /* repeat */
    ASSERT_EQ(rc_reasm_feed(&r, 3u, false, "zz", 2u), RC_ACK_BAD_SEQ);
    ASSERT_EQ(rc_reasm_feed(&r, 2u, true, "e", 1u), RC_ACK_OK);
    ASSERT_TRUE(r.complete);
    ASSERT_STREQ(r.buf, "abcde");
    ASSERT_EQ(rc_reasm_feed(&r, 2u, true, "e", 1u), RC_ACK_OK);         /* lost ACK */
    ASSERT_STREQ(r.buf, "abcde");
    rc_reasm_init(&r);
    ASSERT_EQ(rc_reasm_feed(&r, 0u, false, s_json, 1024u), RC_ACK_OK);
    for (uint8_t s = 1u; s < 6u; s++) {
        ASSERT_EQ(rc_reasm_feed(&r, s, false, s_json, 1024u), RC_ACK_OK);
    }
    ASSERT_EQ(rc_reasm_feed(&r, 6u, true, s_json, 1u), RC_ACK_TOO_BIG);
}

static void test_c6_lines(void) {
    char line[RC_C6_LINE_MAX + 8u];
    make_json("0123456789abcdef", 40u);
    ASSERT_TRUE(rc_c6_begin(line, sizeof line, "0123456789abcdef", 1234u, 0xCBF43926u) > 0u);
    ASSERT_STREQ(line, "RESULT BEGIN 0123456789abcdef 1234 cbf43926");
    /* Every DATA line fits the C6's 80-char cap, and they rebuild the JSON. */
    static char rebuilt[RC_UPLINK_JSON_MAX + 1u];
    size_t pos = 0u;
    const unsigned lines = rc_c6_data_lines(s_json_len);
    for (unsigned i = 0u; i < lines; i++) {
        const size_t n = rc_c6_data(line, sizeof line, s_json, s_json_len, i);
        ASSERT_TRUE(n > 0u && n <= RC_C6_LINE_MAX);
        char prefix[24];
        const int pn = snprintf(prefix, sizeof prefix, "RESULT DATA %u ", i);
        ASSERT_TRUE(strncmp(line, prefix, (size_t)pn) == 0);
        memcpy(rebuilt + pos, line + pn, n - (size_t)pn);
        pos += n - (size_t)pn;
    }
    ASSERT_EQ(rc_c6_data(line, sizeof line, s_json, s_json_len, lines), 0u);
    ASSERT_EQ(pos, s_json_len);
    ASSERT_TRUE(memcmp(rebuilt, s_json, s_json_len) == 0);
    /* The worst case, 4 x 128 turns, still fits 80 chars at the highest seq. */
    ASSERT_TRUE(strlen("RESULT DATA 109 ") + RC_C6_SLICE <= RC_C6_LINE_MAX);
    ASSERT_EQ(crc32_ref("123456789", 9u), 0xCBF43926u);
}

static void test_c6_parse(void) {
    rc_c6_line_t l;
    rc_c6_parse("RESULT OK 0123456789abcdef 201", &l);
    ASSERT_EQ(l.kind, RC_C6_RESULT_OK);
    ASSERT_STREQ(l.id, "0123456789abcdef");
    ASSERT_EQ(l.num, 201u);
    rc_c6_parse("RESULT QUEUED 0123456789abcdef saved", &l);
    ASSERT_EQ(l.kind, RC_C6_RESULT_QUEUED);
    ASSERT_STREQ(l.reason, "saved");
    rc_c6_parse("RESULT ERR 0123456789abcdef crc", &l);
    ASSERT_EQ(l.kind, RC_C6_RESULT_ERR);
    ASSERT_STREQ(l.reason, "crc");
    ASSERT_EQ(l.num, 0u);
    rc_c6_parse("RESULT ERR 0123456789abcdef 401", &l);
    ASSERT_EQ(l.num, 401u);
    rc_c6_parse("WIFI down: password?", &l);
    ASSERT_EQ(l.kind, RC_C6_WIFI);
    ASSERT_EQ(l.wifi, RC_WIFI_DOWN);
    ASSERT_EQ(l.why, RC_WHY_PASSWORD);
    rc_c6_parse("WIFI up", &l);
    ASSERT_EQ(l.wifi, RC_WIFI_UP);
    rc_c6_parse("WIFI off", &l);
    ASSERT_EQ(l.wifi, RC_WIFI_OFF);
    rc_c6_parse("QUEUE 3", &l);
    ASSERT_EQ(l.kind, RC_C6_QUEUE);
    ASSERT_EQ(l.num, 3u);
    rc_c6_parse("HELLO 1.3", &l);
    ASSERT_EQ(l.kind, RC_C6_HELLO);
    rc_c6_parse("TIME 2026-11-21 10:00:00", &l);
    ASSERT_EQ(l.kind, RC_C6_OTHER);
    rc_c6_parse("RESULT OK", &l);
    ASSERT_EQ(l.kind, RC_C6_OTHER);
}

static void test_outbox(void) {
    static rc_outbox_t ob;
    rc_outbox_init(&ob);
    ASSERT_TRUE(rc_outbox_peek(&ob) == NULL);
    ASSERT_TRUE(!rc_outbox_push(&ob, "{\"x\":1}", 7u));      /* no game_id */
    char ids[6][17];
    for (unsigned i = 0u; i < 6u; i++) {
        snprintf(ids[i], sizeof ids[i], "%016x", 0xa0u + i);
        make_json(ids[i], 2u);
        ASSERT_TRUE(rc_outbox_push(&ob, s_json, s_json_len));
        ASSERT_TRUE(rc_outbox_push(&ob, s_json, s_json_len));   /* same id: once */
    }
    ASSERT_EQ(ob.count, RC_OUTBOX_MAX);
    ASSERT_EQ(ob.dropped, 2u);
    ASSERT_STREQ(rc_outbox_peek(&ob)->id, ids[2]);            /* oldest kept */
    rc_outbox_pop(&ob);
    ASSERT_STREQ(rc_outbox_peek(&ob)->id, ids[3]);
    ASSERT_TRUE(strstr(rc_outbox_peek(&ob)->json, ids[3]) != NULL);
}

/* ---- sender, against a scripted main ---- */

static void test_sender(void) {
    static rc_outbox_t ob;
    static rc_sender_t s;
    uint8_t out[RC_CHUNK_HDR + RC_CHUNK_DATA_MAX];
    rc_outbox_init(&ob);
    make_json("feedfacecafebeef", 120u);   /* > 2 chunks */
    ASSERT_TRUE(s_json_len > 2u * RC_CHUNK_DATA_MAX);
    rc_outbox_push(&ob, s_json, s_json_len);
    const rc_outbox_item_t *it = rc_outbox_peek(&ob);
    uint32_t t = 1000u;
    rc_sender_start(&s, it, t);

    /* chunk 0, lost ACK -> resent after RC_SEND_ACK_MS */
    size_t n = rc_sender_poll(&s, it, t, out, sizeof out);
    ASSERT_EQ(n, RC_CHUNK_HDR + RC_CHUNK_DATA_MAX);
    ASSERT_EQ(out[1], 0u);
    ASSERT_EQ(rc_sender_poll(&s, it, t + 10u, out, sizeof out), 0u);
    n = rc_sender_poll(&s, it, t + RC_SEND_ACK_MS, out, sizeof out);
    ASSERT_EQ(out[1], 0u);
    ASSERT_TRUE(n > 0u);
    t += RC_SEND_ACK_MS;
    rc_sender_on_ack(&s, it, 0u, RC_ACK_OK, t);
    /* chunk 1 BAD_SEQ -> restart at 0 */
    n = rc_sender_poll(&s, it, t, out, sizeof out);
    ASSERT_EQ(out[1], 1u);
    rc_sender_on_ack(&s, it, 1u, RC_ACK_BAD_SEQ, t);
    n = rc_sender_poll(&s, it, t, out, sizeof out);
    ASSERT_EQ(out[1], 0u);
    /* then all chunks OK */
    uint8_t seq = 0u;
    for (;;) {
        rc_sender_on_ack(&s, it, seq, RC_ACK_OK, t);
        if (s.phase != RC_SEND_CHUNKS) break;
        n = rc_sender_poll(&s, it, t, out, sizeof out);
        ASSERT_TRUE(n > 0u);
        seq = out[1];
        if (out[2] & 1u) ASSERT_EQ(n - RC_CHUNK_HDR, s_json_len % RC_CHUNK_DATA_MAX);
    }
    ASSERT_EQ(s.phase, RC_SEND_WAIT);
    /* a status about another result changes nothing; ours SAVED finishes */
    rc_uplink_status_t st = {0};
    st.res = RC_RES_SAVED;
    st.tag = 0x12345678u;
    rc_sender_on_status(&s, &st);
    ASSERT_EQ(s.phase, RC_SEND_WAIT);
    st.tag = it->tag;
    rc_sender_on_status(&s, &st);
    ASSERT_EQ(s.phase, RC_SEND_SAVED);

    /* No main at all: gives up after RC_SEND_TRIES */
    rc_sender_start(&s, it, 0u);
    for (uint32_t ms = 0u; ms < 10u * RC_SEND_ACK_MS; ms += 50u) {
        rc_sender_poll(&s, it, ms, out, sizeof out);
    }
    ASSERT_EQ(s.phase, RC_SEND_FAILED);
    ASSERT_EQ(s.why, RC_SENDFAIL_NO_MAIN);

    /* Main never confirms the save: fails after RC_SEND_SAVE_MS */
    rc_sender_start(&s, it, 0u);
    for (seq = 0u; s.phase == RC_SEND_CHUNKS; seq++) {
        rc_sender_poll(&s, it, 0u, out, sizeof out);
        rc_sender_on_ack(&s, it, seq, RC_ACK_OK, 0u);
    }
    rc_sender_poll(&s, it, RC_SEND_SAVE_MS - 1u, out, sizeof out);
    ASSERT_EQ(s.phase, RC_SEND_WAIT);
    rc_sender_poll(&s, it, RC_SEND_SAVE_MS, out, sizeof out);
    ASSERT_EQ(s.phase, RC_SEND_FAILED);
    ASSERT_EQ(s.why, RC_SENDFAIL_UPLINK);
}

/* ---- the GAME OVER line ---- */

static void test_text(void) {
    char buf[64];
    rc_sender_t s;
    rc_sender_init(&s);
    rc_uplink_view_t v = { true, true, {0}, 1u, &s };
    v.st.c6_alive = true;
    v.st.wifi = RC_WIFI_UP;
    ASSERT_EQ(rc_uplink_text(&v, buf, sizeof buf), RC_TONE_INFO);
    ASSERT_STREQ(buf, "WiFi up. Yellow: send");
    v.outbox = 2u;
    v.st.wifi = RC_WIFI_CONNECTING;
    rc_uplink_text(&v, buf, sizeof buf);
    ASSERT_STREQ(buf, "WiFi connecting... Yellow: send 2 games");
    v.st.wifi = RC_WIFI_DOWN;
    v.st.wifi_why = RC_WHY_NOT_FOUND;
    ASSERT_EQ(rc_uplink_text(&v, buf, sizeof buf), RC_TONE_BAD);
    ASSERT_STREQ(buf, "WiFi: network not found. Yellow: save 2 games");
    v.outbox = 0u;
    v.st.wifi = RC_WIFI_UP;
    v.st.res = RC_RES_SAVED;
    v.st.c6_queue = 1u;
    ASSERT_EQ(rc_uplink_text(&v, buf, sizeof buf), RC_TONE_BUSY);
    ASSERT_STREQ(buf, "1 game saved on WiFi module, uploading");
    v.st.res = RC_RES_DELIVERED;
    v.st.c6_queue = 0u;
    ASSERT_EQ(rc_uplink_text(&v, buf, sizeof buf), RC_TONE_GOOD);
    ASSERT_STREQ(buf, "Uploaded to root-metrics");
    v.st.res = RC_RES_REJECTED;
    v.st.http = 401u;
    ASSERT_EQ(rc_uplink_text(&v, buf, sizeof buf), RC_TONE_BAD);
    ASSERT_STREQ(buf, "Upload refused: wrong token in secrets.h");
    v.st.c6_alive = false;
    v.outbox = 1u;
    rc_uplink_text(&v, buf, sizeof buf);
    ASSERT_STREQ(buf, "No WiFi module found. Result kept here");
    v.link_up = false;
    rc_uplink_text(&v, buf, sizeof buf);
    ASSERT_STREQ(buf, "Upload: no answer from the main CPU");
    /* Every message fits 52 columns. */
    for (int w = RC_WIFI_UNKNOWN; w <= RC_WIFI_DOWN; w++) {
        for (int r = RC_RES_NONE; r <= RC_RES_FAILED; r++) {
            for (int d = 0; d <= RC_DETAIL_TIMEOUT; d++) {
                for (int ph = RC_SEND_IDLE; ph <= RC_SEND_FAILED; ph++) {
                    rc_uplink_view_t x = { true, true, {0}, 3u, &s };
                    x.st.c6_alive = true;
                    x.st.wifi = (rc_wifi_t)w;
                    x.st.wifi_why = RC_WHY_SECURITY;
                    x.st.res = (rc_res_t)r;
                    x.st.detail = (rc_detail_t)d;
                    x.st.http = 413u;
                    x.st.c6_queue = 12u;
                    s.phase = (rc_send_phase_t)ph;
                    s.why = RC_SENDFAIL_UPLINK;
                    for (int ob = 0; ob <= 3; ob += 3) {
                        x.outbox = (uint8_t)ob;
                        rc_uplink_text(&x, buf, sizeof buf);
                        ASSERT_TRUE(strlen(buf) <= 52u);
                    }
                }
            }
        }
    }
}

/* ---- the bridge, end to end: fake display <-> bridge <-> fake C6 ---- */

#define MAX_LINES 400u
static char     s_c6_lines[MAX_LINES][RC_C6_LINE_MAX + 8u];
static unsigned s_c6_n;
static uint8_t  s_link[64][RC_STATUS_LEN + 4u];
static size_t   s_link_len[64];
static unsigned s_link_n;
static bool     s_link_ok = true;

static void io_c6(const char *line, size_t len) {
    ASSERT_TRUE(len <= RC_C6_LINE_MAX);
    if (s_c6_n < MAX_LINES) {
        memcpy(s_c6_lines[s_c6_n], line, len);
        s_c6_lines[s_c6_n][len] = '\0';
        s_c6_n++;
    }
}
static bool io_link(const uint8_t *p, size_t n) {
    if (!s_link_ok) return false;
    if (s_link_n < 64u && n <= sizeof s_link[0]) {
        memcpy(s_link[s_link_n], p, n);
        s_link_len[s_link_n++] = n;
    }
    return true;
}
static int find_line(const char *prefix, unsigned from) {
    for (unsigned i = from; i < s_c6_n; i++) {
        if (strncmp(s_c6_lines[i], prefix, strlen(prefix)) == 0) return (int)i;
    }
    return -1;
}
static bool last_status(rc_uplink_status_t *st) {
    for (unsigned i = s_link_n; i-- > 0u;) {
        if (rc_status_decode(s_link[i], s_link_len[i], st)) return true;
    }
    return false;
}

/* What the C6 would rebuild from the lines since `from`; true if the CRC and
 * length announced in BEGIN match. */
static bool c6_check(unsigned from, char *id_out) {
    const int b = find_line("RESULT BEGIN ", from);
    if (b < 0) return false;
    char id[40];
    unsigned long bytes, crc;
    if (sscanf(s_c6_lines[b], "RESULT BEGIN %39s %lu %lx", id, &bytes, &crc) != 3) return false;
    static char buf[RC_UPLINK_JSON_MAX + 1u];
    size_t pos = 0u;
    for (unsigned i = (unsigned)b + 1u; i < s_c6_n; i++) {
        if (strncmp(s_c6_lines[i], "RESULT END ", 11u) == 0) {
            strcpy(id_out, id);
            return pos == bytes && crc32_ref(buf, pos) == crc &&
                   strcmp(s_c6_lines[i] + 11, id) == 0;
        }
        const char *sp = strchr(s_c6_lines[i] + 12, ' ');
        if (!sp) return false;
        const size_t n = strlen(sp + 1);
        memcpy(buf + pos, sp + 1, n);
        pos += n;
    }
    return false;
}

static void test_bridge(void) {
    static rc_bridge_t b;
    static rc_outbox_t ob;
    static rc_sender_t s;
    const rc_bridge_io_t io = { io_c6, io_link };
    s_c6_n = s_link_n = 0u;
    uint32_t t = 0u;
    rc_bridge_init(&b, &io, t);
    ASSERT_STREQ(s_c6_lines[0], "STATUS");
    ASSERT_STREQ(s_c6_lines[1], "WIFI OFF");

    rc_bridge_on_c6_line(&b, "HELLO 1.3", t);
    ASSERT_TRUE(find_line("WIFI OFF", 2u) >= 0);   /* repeated to a fresh C6 */
    rc_bridge_on_c6_line(&b, "WIFI off", t);
    rc_bridge_on_c6_line(&b, "QUEUE 0", t);
    uint8_t wifi_on[2] = { RC_MSG_WIFI, 1u };
    ASSERT_TRUE(rc_bridge_on_link(&b, wifi_on, 2u, t));
    ASSERT_STREQ(s_c6_lines[s_c6_n - 1u], "WIFI ON");
    rc_bridge_on_c6_line(&b, "WIFI connecting", t);
    rc_bridge_on_c6_line(&b, "WIFI up", t);
    rc_bridge_poll(&b, t);
    rc_uplink_status_t st;
    ASSERT_TRUE(last_status(&st));
    ASSERT_TRUE(st.c6_alive);
    ASSERT_EQ(st.wifi, RC_WIFI_UP);

    /* The display hands a result over, chunk by chunk, ACKs looped back. */
    rc_outbox_init(&ob);
    make_json("0badc0de00112233", 120u);
    rc_outbox_push(&ob, s_json, s_json_len);
    const rc_outbox_item_t *it = rc_outbox_peek(&ob);
    rc_sender_start(&s, it, t);
    const unsigned c6_before = s_c6_n;
    for (unsigned guard = 0u; guard < 50u && s.phase == RC_SEND_CHUNKS; guard++) {
        uint8_t out[RC_CHUNK_HDR + RC_CHUNK_DATA_MAX];
        const size_t n = rc_sender_poll(&s, it, t, out, sizeof out);
        if (n) {
            s_link_n = 0u;
            rc_bridge_on_link(&b, out, n, t);
            uint8_t seq; rc_ack_t code;
            ASSERT_TRUE(rc_ack_decode(s_link[0], s_link_len[0], &seq, &code));
            rc_sender_on_ack(&s, it, seq, code, t);
        }
    }
    ASSERT_EQ(s.phase, RC_SEND_WAIT);
    /* A new result now is refused as BUSY until this one is handed over. */
    {
        uint8_t out[8];
        const size_t n = rc_chunk_encode(out, sizeof out, 0u, true, "{}", 2u);
        s_link_n = 0u;
        rc_bridge_on_link(&b, out, n, t);
        uint8_t seq; rc_ack_t code;
        ASSERT_TRUE(rc_ack_decode(s_link[0], s_link_len[0], &seq, &code));
        ASSERT_EQ(code, RC_ACK_BUSY);
    }
    /* Main streams one line per poll; the C6 rebuilds it exactly. */
    for (unsigned i = 0u; i < 200u; i++) rc_bridge_poll(&b, t += 2u);
    char id[40];
    ASSERT_TRUE(c6_check(c6_before, id));
    ASSERT_STREQ(id, "0badc0de00112233");
    for (unsigned i = c6_before; i < s_c6_n; i++) ASSERT_TRUE(strlen(s_c6_lines[i]) <= RC_C6_LINE_MAX);

    /* ERR crc: main resends the whole thing. */
    const unsigned again = s_c6_n;
    rc_bridge_on_c6_line(&b, "RESULT ERR 0badc0de00112233 crc", t);
    for (unsigned i = 0u; i < 200u; i++) rc_bridge_poll(&b, t += 2u);
    ASSERT_TRUE(c6_check(again, id));

    /* QUEUED: saved on the C6. The display's sender sees SAVED for its tag. */
    s_link_n = 0u;
    rc_bridge_on_c6_line(&b, "RESULT QUEUED 0badc0de00112233 saved", t);
    rc_bridge_on_c6_line(&b, "QUEUE 1", t);
    rc_bridge_poll(&b, t);
    ASSERT_TRUE(last_status(&st));
    ASSERT_EQ(st.res, RC_RES_SAVED);
    ASSERT_EQ(st.tag, it->tag);
    ASSERT_EQ(st.c6_queue, 1u);
    rc_sender_on_status(&s, &st);
    ASSERT_EQ(s.phase, RC_SEND_SAVED);
    /* Later the upload succeeds. */
    rc_bridge_on_c6_line(&b, "RESULT OK 0badc0de00112233 201", t);
    rc_bridge_on_c6_line(&b, "QUEUE 0", t);
    rc_bridge_poll(&b, t);
    ASSERT_TRUE(last_status(&st));
    ASSERT_EQ(st.res, RC_RES_DELIVERED);
    ASSERT_EQ(st.http, 201u);
    ASSERT_EQ(st.c6_queue, 0u);

    /* Status is resent once a second even when nothing changed, and a send
       that fails is retried a second later, not every poll. */
    s_link_n = 0u;
    rc_bridge_poll(&b, t + 500u);
    ASSERT_EQ(s_link_n, 0u);
    rc_bridge_poll(&b, t + RC_BRIDGE_STATUS_MS);
    ASSERT_EQ(s_link_n, 1u);

    /* A C6 that goes quiet: no longer alive, and asked for STATUS. */
    const unsigned q = s_c6_n;
    t += RC_BRIDGE_C6_ALIVE_MS + 1u;
    rc_bridge_poll(&b, t);
    ASSERT_TRUE(last_status(&st));
    ASSERT_TRUE(!st.c6_alive);
    ASSERT_TRUE(find_line("STATUS", q) >= 0);

    /* Never-answering C6: the reply wait ends in FAILED / NO_C6. */
    rc_bridge_init(&b, &io, 0u);
    make_json("1111111122222222", 3u);
    uint8_t out[RC_CHUNK_HDR + RC_CHUNK_DATA_MAX];
    size_t n = rc_chunk_encode(out, sizeof out, 0u, true, s_json, s_json_len);
    rc_bridge_on_link(&b, out, n, 0u);
    for (t = 0u; t <= RC_BRIDGE_REPLY_MS + 400u; t += 10u) rc_bridge_poll(&b, t);
    ASSERT_TRUE(last_status(&st));
    ASSERT_EQ(st.res, RC_RES_FAILED);
    ASSERT_EQ(st.detail, RC_DETAIL_NO_C6);
}

int main(void) {
    make_json("7f3a9c21e04b5d18", 2u);
    test_status_roundtrip();
    test_chunk_and_ack();
    test_game_id();
    test_reassembly();
    test_c6_lines();
    test_c6_parse();
    test_outbox();
    test_sender();
    test_text();
    test_bridge();
    TEST_RETURN();
}
