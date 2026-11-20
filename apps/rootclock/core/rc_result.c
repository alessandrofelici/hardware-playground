/* rootclock -- GameResult and JSON serializer. STUB: implemented on feat/rootclock-core. */
#include "rc_result.h"

void rc_result_build(rc_result_t *r, const rc_game_t *g, uint32_t now_ms, uint64_t id_bits) {
    (void)r; (void)g; (void)now_ms; (void)id_bits;
}
size_t rc_result_to_json(const rc_result_t *r, char *buf, size_t cap) {
    (void)r;
    if (cap > 0u) buf[0] = '\0';
    return 0u;
}
