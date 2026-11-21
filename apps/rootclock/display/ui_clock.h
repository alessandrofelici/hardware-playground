/* rootclock -- ClockScreen: S1, the player clock, in every state.
 *
 * Layout (documents/02, S1 pixel table; 320x240, black background):
 *   Top bar   0,0,320,24    "GAME H:MM:SS" left (scale 2); state badge right
 *                           (READY / RUNNING / PAUSED / GAME OVER, colours
 *                           from ui_palette.h)
 *   Icon tile 8,30,96,96    faction icon of the shown seat (rc_icon_get)
 *   Name      116,30        "P<seat+1> <nickname>"          scale 3
 *   Average   116,60        "AVG/TURN <MM:SS>", the shown seat's average
 *                           COMPLETED turn (rc_game_avg_turn_ms), or
 *                           "AVG/TURN --:--" before its first turn ends;
 *                           scale 2. (002: replaces the faction name, which
 *                           the icon already shows.)
 *   Total     116,82        "TOTAL H:MM:SS"                 scale 2
 *   Turn info 116,104       "TURN <n>  LIM <m:ss|off>"      scale 2
 *   Big timer 40,140,240,64 centred on x=160, top y=140:
 *                           rc_fmt_turn() at scale 8 if it is 5 chars,
 *                           scale 6 if longer (H:MM:SS)
 *   Hint row  0,220,320,20  12x12 colour swatch + label per live button
 *
 * Which card S1 shows is rc_game_shown_seat(): the active seat while
 * RUNNING, the viewed seat while PAUSED or OVER (green cycles it).
 *
 * What the big timer shows: READY -> "00:00" white; RUNNING -> the active
 * turn, UI_OVER when rc_game_turn_over_limit() else UI_FG; PAUSED -> the
 * paused turn in UI_DIM when the shown card is the active seat's, otherwise
 * that seat's TOTAL in UI_DIM (its turn clock is not running, so 00:00 would
 * say nothing); OVER -> the viewed seat's TOTAL formatted with rc_fmt_turn(),
 * UI_FG.
 *
 * Hint row text per state (swatch colour in brackets):
 *   READY    [red] start  [yellow] settings
 *   RUNNING  [green] next  [red] pause
 *   PAUSED   [red] resume  [green] view  [yellow] settings  [blue] hold: end
 *            (scale 1: it does not fit at scale 2)
 *   OVER     [green] view  [blue] hold: new game
 *   while hold_progress > 0: "keep holding..." replaces the row text.
 *
 * Each field is cached and redrawn only when its text or colour changes, so a
 * steady RUNNING screen repaints one or two big glyphs and a few small
 * strings per second. */
#ifndef UI_CLOCK_H
#define UI_CLOCK_H
#include <stdbool.h>
#include <stdint.h>
#include "rc_game.h"

/* Draw S1 for `g` at now_ms. hold_progress is rc_input_hold_progress()
 * (0..100). force: clear the whole screen and redraw every field -- call it
 * whenever S1 becomes the visible screen. */
void ui_clock_render(const rc_game_t *g, uint32_t now_ms,
                     unsigned hold_progress, bool force);

#endif
