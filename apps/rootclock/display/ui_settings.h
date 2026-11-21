/* rootclock -- SettingsScreen: S2.
 *
 * Layout (documents/02, S2 pixel table; 320x240):
 *   Title bar 0,0,320,24     "SETTINGS" (scale 2)
 *   Row list  0,28,204,190   10 rows, 19 px pitch, scale 2:
 *                              row 0  "PLAYERS <3|4>"
 *                              row 1  "LIMIT <m:ss|off>"
 *                              row 2  "P1 <nickname>"   row 3  "P1 <faction short>"
 *                              ... through row 9 "P4 <faction short>"
 *                            selected row: UI_SEL_BG background, a leading
 *                            '>' and its value written "<value>"; the rows
 *                            of a seat outside the rotation (P4 with 3
 *                            players) are drawn in UI_DIM
 *   Preview   212,28,104,190 for a seat row: that seat's faction icon
 *                            (96x96 at 216,40) and nickname under it
 *                            (scale 2); for the LIMIT row: short scale-1
 *                            explanation ("beep + red at" / "limit, keeps" /
 *                            "counting"); for the PLAYERS row: the green
 *                            button's rotation ("green: next turn" /
 *                            "P1>P2>P3>P1" / "P4 sits out")
 *   Hint row  0,220,320,20   [gray] up [red] dn [yellow] - [blue] + [green] done
 *
 * Rows and preview are cached and redrawn only when they change. */
#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H
#include <stdbool.h>
#include "rc_game.h"

/* Draw S2 from g->settings and g->menu. force: clear the whole screen and
 * redraw everything -- call it whenever S2 becomes the visible screen. */
void ui_settings_render(const rc_game_t *g, bool force);

#endif
