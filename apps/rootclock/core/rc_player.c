/* rootclock -- Player and TurnLog. STUB: implemented on feat/rootclock-core. */
#include "rc_player.h"

void rc_turnlog_clear(rc_turnlog_t *log) { (void)log; }
void rc_turnlog_append(rc_turnlog_t *log, uint32_t ms) { (void)log; (void)ms; }
uint32_t rc_turnlog_sum_ms(const rc_turnlog_t *log) { (void)log; return 0u; }

void rc_player_init(rc_player_t *p, uint8_t seat) { (void)p; (void)seat; }
void rc_player_begin_turn(rc_player_t *p, uint32_t now_ms) { (void)p; (void)now_ms; }
void rc_player_end_turn(rc_player_t *p, uint32_t now_ms) { (void)p; (void)now_ms; }
void rc_player_pause(rc_player_t *p, uint32_t now_ms) { (void)p; (void)now_ms; }
void rc_player_resume(rc_player_t *p, uint32_t now_ms) { (void)p; (void)now_ms; }
uint32_t rc_player_turn_ms(const rc_player_t *p, uint32_t now_ms) { (void)p; (void)now_ms; return 0u; }
uint32_t rc_player_total_ms(const rc_player_t *p, uint32_t now_ms) { (void)p; (void)now_ms; return 0u; }
