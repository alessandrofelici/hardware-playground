/* rootclock -- main CPU: OrcaBridge, the relay between the display (inter-CPU
 * link) and the Bottlenose ESP32-C6 (UART1 lines). Portable: no SDK calls,
 * time arrives as now_ms and all I/O goes through rc_bridge_io_t, so
 * main/main.c runs it on the board and tools/sim runs the same code on a PC.
 *
 *   display 0x50 chunks -> reassemble -> ACK 0x51 each
 *   complete            -> stream "RESULT BEGIN / DATA x n / END" to the C6,
 *                          one line per poll (main kicks its watchdog between)
 *   C6 replies          -> track it, report 0x52 UPLINK_STATUS to the display
 *                          (on every change and once a second)
 *   display 0x53 WIFI   -> "WIFI ON" / "WIFI OFF" to the C6
 *
 * At boot it asks the C6 for STATUS and sends WIFI OFF: the radio stays off
 * while a game is played and comes on when the display asks, at GAME OVER.
 * The C6 treats WIFI OFF as "off once nothing is waiting to upload". */
#ifndef RC_BRIDGE_H
#define RC_BRIDGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rc_uplink.h"

#define RC_BRIDGE_STATUS_MS     1000u   /* status to the display, at least  */
#define RC_BRIDGE_C6_ALIVE_MS   40000u  /* C6 says STATUS every 30 s        */
#define RC_BRIDGE_C6_PING_MS    10000u  /* ask a silent C6 for STATUS       */
#define RC_BRIDGE_REPLY_MS      20000u  /* END -> QUEUED/OK/ERR             */
#define RC_BRIDGE_C6_TRIES      3u      /* resends after ERR crc|len        */

typedef struct {
    /* One line to the C6; the callee adds the '\n'. */
    void (*c6_line)(const char *line, size_t len);
    /* One link payload to the display; false if it could not be sent. */
    bool (*link_send)(const uint8_t *payload, size_t len);
} rc_bridge_io_t;

typedef enum {
    RC_STREAM_IDLE = 0,
    RC_STREAM_BEGIN,
    RC_STREAM_DATA,
    RC_STREAM_END,
    RC_STREAM_WAIT     /* END sent, waiting for the C6's verdict */
} rc_stream_t;

typedef struct {
    rc_bridge_io_t     io;
    rc_reasm_t         rx;            /* the result being received / handed */
    char               id[RC_ID_MAX + 1u];
    uint32_t           crc;
    rc_stream_t        stream;
    unsigned           line;          /* next RESULT DATA seq               */
    uint8_t            tries;
    uint32_t           end_ms;        /* when END went out                  */
    rc_uplink_status_t st;            /* what the display is told           */
    rc_uplink_status_t st_sent;
    uint32_t           st_sent_ms;
    bool               st_ever_sent;
    bool               c6_heard;
    uint32_t           c6_last_ms;
    uint32_t           ping_ms;
    int8_t             wifi_want;     /* -1 none yet, 0 off, 1 on           */
    uint32_t           lines_in;
    uint32_t           status_drops;
} rc_bridge_t;

void rc_bridge_init(rc_bridge_t *b, const rc_bridge_io_t *io, uint32_t now_ms);
/* A payload from the display (any type; others are ignored, returns false). */
bool rc_bridge_on_link(rc_bridge_t *b, const uint8_t *p, size_t n, uint32_t now_ms);
/* One line from the C6, without its line ending. */
void rc_bridge_on_c6_line(rc_bridge_t *b, const char *line, uint32_t now_ms);
/* Call every loop iteration. */
void rc_bridge_poll(rc_bridge_t *b, uint32_t now_ms);

#endif
