/* rootclock -- GameResult snapshot and the contract v1 JSON serializer.
 *
 * The JSON is EXACTLY payload contract v1 (documents/04 section 2), compact
 * (no spaces, no newline), keys in this order:
 *
 *   {"schema":"rootclock.game_result","schema_version":1,
 *    "game_id":"<16 lowercase hex>","device":{"app":"rootclock",
 *    "app_version":"001"},"ended_at":null,"turn_limit_ms":<int|null>,
 *    "global_ms":<int>,"players":[{"seat":1,"nickname":"Jacob",
 *    "faction":"marquise","total_ms":<int>,"turns_ms":[<int>,...],
 *    "turns_truncated":false},...]}
 *
 * Only seats with at least one COMPLETED turn appear, in seat order. Build the
 * result in OVER, where every turn has been ended and logged, so for each
 * player total_ms == sum(turns_ms) and global_ms == sum(total_ms). The MVP
 * always writes "ended_at":null and "turn_limit_ms":null when the limit is
 * off. */
#ifndef RC_RESULT_H
#define RC_RESULT_H
#include "rc_game.h"
#include "rc_types.h"

#define RC_GAME_ID_HEX   16u     /* chars, lowercase hex                     */
#define RC_RESULT_JSON_MAX 6144u /* worst case is ~4.7 KB (4 x 128 turns)    */

typedef struct {
    uint8_t         seat;         /* 1..4 (exported form)                   */
    const char     *nickname;
    const char     *faction_key;
    uint32_t        total_ms;
    const uint32_t *turns_ms;     /* points into the game's log             */
    uint16_t        turn_count;
    bool            truncated;
} rc_result_player_t;

typedef struct {
    char               game_id[RC_GAME_ID_HEX + 1u];
    uint32_t           limit_ms;      /* 0 -> "turn_limit_ms":null          */
    uint32_t           global_ms;
    uint8_t            player_count;
    rc_result_player_t players[RC_SEATS];
} rc_result_t;

/* Snapshot `g` at now_ms. `id_bits` supplies the game id (the display passes
 * a random 64-bit value; it is formatted as 16 lowercase hex chars, most
 * significant nibble first). The result borrows the game's log arrays, so it
 * is valid only while `g` is unchanged. */
void rc_result_build(rc_result_t *r, const rc_game_t *g, uint32_t now_ms,
                     uint64_t id_bits);

/* Write the compact JSON plus a terminating NUL into buf. Returns the length
 * excluding the NUL, or 0 (with buf[0] = '\0' when cap > 0) if it does not
 * fit in `cap` bytes. */
size_t rc_result_to_json(const rc_result_t *r, char *buf, size_t cap);

#endif
