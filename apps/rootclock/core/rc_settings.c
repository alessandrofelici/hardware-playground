/* rootclock -- Settings and SettingsMenu. STUB: implemented on feat/rootclock-core. */
#include "rc_settings.h"

void rc_settings_defaults(rc_settings_t *s) { (void)s; }
uint32_t rc_limit_option_ms(uint8_t idx) { (void)idx; return 0u; }
uint32_t rc_settings_limit_ms(const rc_settings_t *s) { (void)s; return 0u; }
void rc_settings_cycle_limit(rc_settings_t *s, int dir) { (void)s; (void)dir; }
void rc_settings_cycle_nickname(rc_settings_t *s, uint8_t seat, int dir) { (void)s; (void)seat; (void)dir; }
void rc_settings_cycle_faction(rc_settings_t *s, uint8_t seat, int dir) { (void)s; (void)seat; (void)dir; }

void rc_menu_reset(rc_menu_t *m) { (void)m; }
void rc_menu_move(rc_menu_t *m, int dir) { (void)m; (void)dir; }
void rc_menu_change(const rc_menu_t *m, rc_settings_t *s, int dir) { (void)m; (void)s; (void)dir; }
rc_row_kind_t rc_menu_row_kind(uint8_t row) { (void)row; return RC_ROW_LIMIT; }
uint8_t rc_menu_row_seat(uint8_t row) { (void)row; return RC_NO_SEAT; }
