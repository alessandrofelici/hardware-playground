/* rootclock -- display CPU: the upload's display half. See export.h. */
#include "export.h"

#include <string.h>

#include "fwog_display.h"
#include "rc_link.h"

#define LINK_SEND_TIMEOUT_US 20000u   /* a chunk is ~1 KB at 12.5 Mbaud    */
#define HEARD_MS             3000u    /* main sends status once a second   */
#define WIFI_RESEND_MS       5000u

static bool           s_link_up;
static fwog_link_rx_t s_rx;           /* ~4.2 KB: file scope, not stack    */
static rc_outbox_t    s_outbox;       /* RC_OUTBOX_MAX x ~6 KB             */
static rc_sender_t    s_sender;
static bool           s_sending;      /* yellow was pressed; work through   */
static rc_uplink_status_t s_st;
static bool           s_heard;
static uint32_t       s_heard_ms;
static int8_t         s_wifi_sent = -1;
static uint32_t       s_wifi_sent_ms;

void export_init(uint32_t now_ms) {
    s_link_up = fwog_link_uart_init(FWOG_LINK_BAUD);
    fwog_link_rx_init(&s_rx);
    rc_outbox_init(&s_outbox);
    rc_sender_init(&s_sender);
    memset(&s_st, 0, sizeof s_st);
    s_heard = false;
    s_wifi_sent = -1;
    s_wifi_sent_ms = now_ms;
    DIAG("[rootclock_display] link=%s\n", s_link_up ? "ok" : "FAILED");
}

void export_add(const char *json, size_t len) {
    const uint32_t dropped = s_outbox.dropped;
    if (!rc_outbox_push(&s_outbox, json, len)) {
        DIAG("UPLINK not queued: no game_id or too long\n");
    } else if (s_outbox.dropped != dropped) {
        DIAG("UPLINK outbox full: oldest unsent result dropped\n");
    }
}

static void start_next(uint32_t now_ms) {
    const rc_outbox_item_t *it = rc_outbox_peek(&s_outbox);
    if (it) {
        rc_sender_start(&s_sender, it, now_ms);
        DIAG("UPLINK sending %s (%u bytes)\n", it->id, (unsigned)it->len);
    } else {
        s_sending = false;
    }
}

void export_send(uint32_t now_ms) {
    if (!s_link_up || s_outbox.count == 0u) return;
    if (s_sending && (s_sender.phase == RC_SEND_CHUNKS || s_sender.phase == RC_SEND_WAIT)) {
        return;   /* already on it */
    }
    s_sending = true;
    start_next(now_ms);
}

static void on_payload(const uint8_t *p, size_t n, uint32_t now_ms) {
    uint8_t seq;
    rc_ack_t code;
    rc_uplink_status_t st;
    if (rc_ack_decode(p, n, &seq, &code)) {
        rc_sender_on_ack(&s_sender, rc_outbox_peek(&s_outbox), seq, code, now_ms);
    } else if (rc_status_decode(p, n, &st)) {
        s_st = st;
        s_heard = true;
        s_heard_ms = now_ms;
        rc_sender_on_status(&s_sender, &st);
    } else {
        /* Not ours: the BSP's breakout-direction messages, if main sends any. */
        (void)fwog_ioexp_link_handle(p, n);
    }
}

static void wifi_want(bool on, uint32_t now_ms) {
    const int8_t want = on ? 1 : 0;
    if (want == s_wifi_sent && now_ms - s_wifi_sent_ms < WIFI_RESEND_MS) return;
    const uint8_t msg[2] = { RC_MSG_WIFI, (uint8_t)want };
    if (rc_link_send(msg, sizeof msg, LINK_SEND_TIMEOUT_US)) {
        s_wifi_sent = want;
        s_wifi_sent_ms = now_ms;
    }
}

void export_poll(rc_state_t state, uint32_t now_ms) {
    if (!s_link_up) return;
    uint8_t b;
    while (fwog_link_uart_read(&b)) {
        size_t len = 0u;
        if (fwog_link_rx_byte(&s_rx, b, &len)) on_payload(s_rx.buf, len, now_ms);
    }
    if (s_heard && now_ms - s_heard_ms >= HEARD_MS) s_heard = false;

    /* WiFi on while the game is over, so it is ready when yellow is pressed. */
    wifi_want(state == RC_STATE_OVER, now_ms);

    const rc_outbox_item_t *it = rc_outbox_peek(&s_outbox);
    if (!s_sending || !it) return;
    static uint8_t frame_payload[RC_CHUNK_HDR + RC_CHUNK_DATA_MAX];
    const size_t n = rc_sender_poll(&s_sender, it, now_ms, frame_payload, sizeof frame_payload);
    if (n && !rc_link_send(frame_payload, n, LINK_SEND_TIMEOUT_US)) {
        /* Not sent whole: the ACK timeout resends it. */
    }
    if (s_sender.phase == RC_SEND_SAVED) {
        DIAG("UPLINK saved on the WiFi module: %s\n", it->id);
        rc_outbox_pop(&s_outbox);
        start_next(now_ms);
    } else if (s_sender.phase == RC_SEND_FAILED) {
        DIAG("UPLINK not handed over (%d): %s stays here\n", (int)s_sender.why, it->id);
        s_sending = false;
    }
}

void export_view(rc_uplink_view_t *v, uint32_t now_ms) {
    (void)now_ms;
    v->link_up = s_link_up;
    v->main_heard = s_heard;
    v->st = s_st;
    v->outbox = s_outbox.count;
    v->sender = &s_sender;
}

uint8_t export_waiting(void) { return s_outbox.count; }
