/* rootclock -- Uplink: getting a finished game from the display CPU, through
 * the main CPU and the Bottlenose ESP32-C6, to root-metrics. Pure (no SDK):
 * the message formats and the state machines both halves run, so all of it is
 * host-tested. documents/04 describes the whole path.
 *
 *   display --link 0x50 RESULT_CHUNK--> main --UART1 "RESULT ..." lines--> C6
 *   display <--link 0x51 RESULT_ACK---- main                                |
 *   display <--link 0x52 UPLINK_STATUS- main <--"RESULT OK/QUEUED/ERR"------+
 *   display --link 0x53 WIFI on/off---> main --"WIFI ON|OFF"--------------> C6
 *
 * Link payloads (byte 0 is the type; app types start at 0x40, bottlenose uses
 * 0x40/0x41 in its own image):
 *   0x50 RESULT_CHUNK  display->main [type][seq][flags][<= 1024 bytes JSON]
 *                      flags bit 0: last chunk. seq 0 starts a new result.
 *   0x51 RESULT_ACK    main->display [type][seq][rc_ack_t]
 *   0x52 UPLINK_STATUS main->display [type][c6][wifi][wifi_why][res][detail]
 *                      [http lo][http hi][c6_queue][tag 4 bytes LE]  (14 B,
 *                      under the 32-byte UART FIFO, so sending it never waits)
 *   0x53 WIFI          display->main [type][1 = on, 0 = off when idle]
 *
 * C6 lines (UART1, 115200 8N1, the C6 caps inbound lines at 80 chars):
 *   OG -> C6  RESULT BEGIN <id> <bytes> <crc32 hex>
 *             RESULT DATA <seq> <<= 56 chars of the JSON>
 *             RESULT END <id>
 *             WIFI ON | WIFI OFF | STATUS
 *   C6 -> OG  RESULT QUEUED <id> <why>    saved to the C6's flash; uploading
 *             RESULT OK <id> <http>       the server has it (201, or 200 dup)
 *             RESULT ERR <id> <reason>    crc | len | nourl | <http code>
 *             WIFI connecting | up | off | down: <why>
 *             QUEUE <n>                   results saved on the C6, not yet
 *                                         delivered
 *             HELLO <version> */
#ifndef RC_UPLINK_H
#define RC_UPLINK_H
#include "rc_types.h"

#define RC_MSG_RESULT_CHUNK  0x50u
#define RC_MSG_RESULT_ACK    0x51u
#define RC_MSG_UPLINK_STATUS 0x52u
#define RC_MSG_WIFI          0x53u

#define RC_UPLINK_JSON_MAX   6144u   /* == RC_RESULT_JSON_MAX              */
#define RC_CHUNK_DATA_MAX    1024u
#define RC_CHUNK_HDR         3u      /* type, seq, flags                   */
#define RC_STATUS_LEN        14u
#define RC_C6_SLICE          56u     /* JSON chars per RESULT DATA line    */
#define RC_C6_LINE_MAX       80u     /* the C6's inbound line cap          */
#define RC_ID_MAX            32u     /* contract v1 game_id maxLength      */

/* ---- 0x51 ack codes ---- */
typedef enum {
    RC_ACK_OK = 0,       /* stored (or a repeat of the chunk just stored)  */
    RC_ACK_BAD_SEQ = 1,  /* out of order: the display restarts from 0      */
    RC_ACK_TOO_BIG = 2,  /* over RC_UPLINK_JSON_MAX                        */
    RC_ACK_BUSY = 3      /* main is still handing the previous one over    */
} rc_ack_t;

/* ---- 0x52 status ---- */
typedef enum {
    RC_WIFI_UNKNOWN = 0,
    RC_WIFI_OFF,
    RC_WIFI_CONNECTING,
    RC_WIFI_UP,
    RC_WIFI_DOWN
} rc_wifi_t;

typedef enum {               /* why WIFI is down, from the C6's text       */
    RC_WHY_NONE = 0,
    RC_WHY_NOT_FOUND,
    RC_WHY_PASSWORD,
    RC_WHY_SECURITY,
    RC_WHY_LOST,
    RC_WHY_OTHER
} rc_wifi_why_t;

typedef enum {
    RC_RES_NONE = 0,     /* main holds nothing                             */
    RC_RES_RECEIVING,    /* chunks are arriving from the display           */
    RC_RES_TO_C6,        /* main is handing it to the C6                   */
    RC_RES_SAVED,        /* on the C6's flash: safe, the C6 uploads it     */
    RC_RES_DELIVERED,    /* the server has it (http 201 or 200)            */
    RC_RES_REJECTED,     /* the server refused it (http 4xx)               */
    RC_RES_FAILED        /* not handed over: see rc_detail_t               */
} rc_res_t;

typedef enum {
    RC_DETAIL_NONE = 0,
    RC_DETAIL_NO_C6,     /* the C6 never answered                          */
    RC_DETAIL_CRC,       /* the C6 got it garbled three times              */
    RC_DETAIL_NO_URL,    /* the C6 has no RESULTS_URL in secrets.h         */
    RC_DETAIL_TIMEOUT    /* the C6 went quiet before saying OK or QUEUED   */
} rc_detail_t;

typedef struct {
    bool          c6_alive;
    rc_wifi_t     wifi;
    rc_wifi_why_t wifi_why;
    rc_res_t      res;
    rc_detail_t   detail;
    uint16_t      http;       /* last HTTP status for `tag`, 0 if none     */
    uint8_t       c6_queue;   /* results waiting on the C6's flash         */
    uint32_t      tag;        /* rc_id_tag() of the result `res` is about  */
} rc_uplink_status_t;

size_t rc_status_encode(const rc_uplink_status_t *s, uint8_t *out, size_t cap);
bool   rc_status_decode(const uint8_t *p, size_t n, rc_uplink_status_t *s);

/* ---- 0x50 / 0x51 / 0x53 ---- */
size_t rc_chunk_encode(uint8_t *out, size_t cap, uint8_t seq, bool last,
                       const char *data, size_t n);
bool   rc_chunk_decode(const uint8_t *p, size_t n, uint8_t *seq, bool *last,
                       const char **data, size_t *dn);
size_t rc_ack_encode(uint8_t *out, size_t cap, uint8_t seq, rc_ack_t code);
bool   rc_ack_decode(const uint8_t *p, size_t n, uint8_t *seq, rc_ack_t *code);

/* ---- game id ---- */
/* The value of "game_id" in a contract-v1 JSON (1..32 chars, no escapes).
 * out holds RC_ID_MAX + 1 bytes. */
bool     rc_json_game_id(const char *json, size_t n, char *out);
/* A 32-bit tag for an id: its first 8 hex digits, else an FNV-1a hash. */
uint32_t rc_id_tag(const char *id);

/* ---- main: reassembly of 0x50 chunks ---- */
typedef struct {
    char     buf[RC_UPLINK_JSON_MAX + 1u];
    size_t   len;
    uint8_t  next_seq;
    bool     active;     /* seq 0 seen, last not yet                         */
    bool     complete;   /* last chunk stored; buf is NUL-terminated         */
} rc_reasm_t;

void     rc_reasm_init(rc_reasm_t *r);
/* seq 0 always starts over. A repeat of the chunk just stored (its ACK was
 * lost) is acknowledged again without being appended. */
rc_ack_t rc_reasm_feed(rc_reasm_t *r, uint8_t seq, bool last, const char *data,
                       size_t n);

/* ---- main <-> C6 lines ---- */
size_t rc_c6_begin(char *out, size_t cap, const char *id, size_t bytes, uint32_t crc);
/* RESULT DATA line `seq` for json[seq*56 ..]; 0 when seq is past the end. */
size_t rc_c6_data(char *out, size_t cap, const char *json, size_t len, unsigned seq);
size_t rc_c6_end(char *out, size_t cap, const char *id);
unsigned rc_c6_data_lines(size_t len);

typedef enum {
    RC_C6_OTHER = 0,
    RC_C6_HELLO,
    RC_C6_WIFI,
    RC_C6_RESULT_QUEUED,
    RC_C6_RESULT_OK,
    RC_C6_RESULT_ERR,
    RC_C6_QUEUE
} rc_c6_kind_t;

typedef struct {
    rc_c6_kind_t  kind;
    char          id[RC_ID_MAX + 1u];
    unsigned      num;         /* http code (OK / ERR <code>), QUEUE count  */
    rc_wifi_t     wifi;
    rc_wifi_why_t why;
    char          reason[16];  /* ERR reason word: "crc", "len", "nourl"... */
} rc_c6_line_t;

void rc_c6_parse(const char *line, rc_c6_line_t *out);

/* ---- display: results waiting to go ----
 * A small queue in RAM, oldest first. A game that ends while older ones are
 * still unsent is added behind them; only when all RC_OUTBOX_MAX slots are
 * full is the oldest dropped (and counted). */
#define RC_OUTBOX_MAX 4u

typedef struct {
    char     json[RC_UPLINK_JSON_MAX + 1u];
    uint16_t len;
    char     id[RC_ID_MAX + 1u];
    uint32_t tag;
} rc_outbox_item_t;

typedef struct {
    rc_outbox_item_t item[RC_OUTBOX_MAX];
    uint8_t          head;
    uint8_t          count;
    uint32_t         dropped;
} rc_outbox_t;

void rc_outbox_init(rc_outbox_t *ob);
/* False if json is too long or has no game_id. Adding a game_id already in
 * the box replaces nothing and returns true. */
bool rc_outbox_push(rc_outbox_t *ob, const char *json, size_t len);
const rc_outbox_item_t *rc_outbox_peek(const rc_outbox_t *ob);
void rc_outbox_pop(rc_outbox_t *ob);

/* ---- display: handing the oldest result to main ----
 * One chunk in flight at a time. A chunk not acknowledged within
 * RC_SEND_ACK_MS is sent again, up to RC_SEND_TRIES times; BAD_SEQ restarts
 * from chunk 0 (counted as a try). After the last ACK, wait up to
 * RC_SEND_SAVE_MS for main to report SAVED, DELIVERED or a final error for
 * this result's tag. */
#define RC_SEND_ACK_MS   300u
#define RC_SEND_TRIES    4u
#define RC_SEND_SAVE_MS  30000u

typedef enum {
    RC_SEND_IDLE = 0,
    RC_SEND_CHUNKS,      /* transferring to main                            */
    RC_SEND_WAIT,        /* all chunks acknowledged; main hands it to the C6 */
    RC_SEND_SAVED,       /* the C6 has it: pop it from the outbox           */
    RC_SEND_FAILED       /* see `why`; the result stays in the outbox       */
} rc_send_phase_t;

typedef enum {
    RC_SENDFAIL_NONE = 0,
    RC_SENDFAIL_NO_MAIN,     /* no ACK from the main CPU                     */
    RC_SENDFAIL_REFUSED,     /* main said TOO_BIG or stayed BUSY             */
    RC_SENDFAIL_UPLINK       /* main reported RC_RES_FAILED / REJECTED       */
} rc_sendfail_t;

typedef struct {
    rc_send_phase_t phase;
    rc_sendfail_t   why;
    uint32_t        tag;
    size_t          off;          /* start of the chunk in flight          */
    size_t          chunk_len;
    uint8_t         seq;
    uint8_t         tries;
    bool            awaiting;     /* a chunk is out, no ACK yet            */
    uint32_t        sent_ms;
    uint32_t        wait_since_ms;
} rc_sender_t;

void rc_sender_init(rc_sender_t *s);
/* Begin handing `item` over. */
void rc_sender_start(rc_sender_t *s, const rc_outbox_item_t *item, uint32_t now_ms);
/* The payload to put on the link now, if any (a new chunk or a retry), into
 * out (>= RC_CHUNK_HDR + RC_CHUNK_DATA_MAX bytes). Also moves to FAILED on
 * the ACK or save timeouts. */
size_t rc_sender_poll(rc_sender_t *s, const rc_outbox_item_t *item,
                      uint32_t now_ms, uint8_t *out, size_t cap);
void rc_sender_on_ack(rc_sender_t *s, const rc_outbox_item_t *item,
                      uint8_t seq, rc_ack_t code, uint32_t now_ms);
void rc_sender_on_status(rc_sender_t *s, const rc_uplink_status_t *st);

/* ---- display: the line shown on the GAME OVER screen ---- */
typedef enum {
    RC_TONE_INFO = 0,   /* white  */
    RC_TONE_BUSY,       /* yellow */
    RC_TONE_GOOD,       /* green  */
    RC_TONE_BAD         /* red    */
} rc_tone_t;

typedef struct {
    bool               link_up;      /* the display's link UART came up    */
    bool               main_heard;   /* a status arrived in the last 3 s   */
    rc_uplink_status_t st;           /* the latest one                     */
    uint8_t            outbox;       /* results not yet handed over        */
    const rc_sender_t *sender;
} rc_uplink_view_t;

/* At most 52 chars (scale 1 across the screen). Returns the tone. */
rc_tone_t rc_uplink_text(const rc_uplink_view_t *v, char *out, size_t cap);

#endif
