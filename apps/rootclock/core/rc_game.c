/* rootclock -- GameClock. STUB: implemented on feat/rootclock-core. */
#include "rc_game.h"

void rc_game_init(rc_game_t *g) { (void)g; }
rc_events_t rc_game_apply(rc_game_t *g, rc_action_t a, uint32_t now_ms) { (void)g; (void)a; (void)now_ms; return RC_EVT_NONE; }
rc_events_t rc_game_tick(rc_game_t *g, uint32_t now_ms) { (void)g; (void)now_ms; return RC_EVT_NONE; }
uint32_t rc_game_turn_ms(const rc_game_t *g, uint8_t seat, uint32_t now_ms) { (void)g; (void)seat; (void)now_ms; return 0u; }
uint32_t rc_game_total_ms(const rc_game_t *g, uint8_t seat, uint32_t now_ms) { (void)g; (void)seat; (void)now_ms; return 0u; }
uint32_t rc_game_global_ms(const rc_game_t *g, uint32_t now_ms) { (void)g; (void)now_ms; return 0u; }
uint8_t rc_game_shown_seat(const rc_game_t *g) { (void)g; return 0u; }
bool rc_game_turn_over_limit(const rc_game_t *g, uint32_t now_ms) { (void)g; (void)now_ms; return false; }
