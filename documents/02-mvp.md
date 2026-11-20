# 02 — MVP: rootclock

The MVP is exactly what was requested, and nothing more. Every feature below traces to a
requirement ID in [01-spec-review.md](01-spec-review.md), and every acceptance test is a step
you can perform on the board to show the feature works.

## Goal

A FreeWili OG that keeps time for a 3–4 player board game:

- one button per player starts that player's turn;
- red pauses and resumes the clock;
- each player's turn time, total time and faction icon are shown on their clock screen;
- a settings page sets a per-turn time limit, each player's faction and each player's
  nickname.

## In scope

| # | Feature | Requirement |
|---|---|---|
| F1 | Gray/yellow/green/blue switch the clock to player 1/2/3/4 | R-BTN-1 |
| F2 | Red starts the game, pauses it and resumes it | R-BTN-2 |
| F3 | Holding blue for 2 s while paused ends the game and freezes every clock | R-BTN-3 |
| F4 | Yellow opens settings while the game is READY or PAUSED | R-BTN-4 |
| F5 | A per-player turn timer that starts at 00:00 each turn | R-TMR-1, R-TMR-2 |
| F6 | A total time per player that accumulates across turns | R-TMR-3 |
| F7 | A global game timer; pausing stops it | R-TMR-4 |
| F8 | Per-turn time limit: one beep and white → red digits at the limit, and the digits keep counting | R-SET-1 |
| F9 | Each player's faction icon, chosen from 13, shown on their clock screen | R-SET-2 |
| F10 | Each player's nickname, chosen from Jacob, Alex, Ahmed and Lorenzo | R-SET-3 |
| F11 | S1, the player clock screen | R-SCR-1 |
| F12 | S2, the settings screen | R-SCR-2 |
| F13 | The BSP rules: red held 6 s powers off, the main CPU watchdog is kicked, VERSION/DESCRIPTION are set | R-SYS-1..4 |

## Out of scope

- Any ESP32 or mobile export (planned in [04](04-future-esp32-integration.md)).
- Saving settings across power-off.
- LED-bar effects beyond the BSP's power-off countdown.
- A results or summary screen. GAME OVER reuses S1.
- A player-count setting. A seat that never takes a turn simply does not appear.
- Sound beyond the single time-limit beep.
- Undo, or editing turn times.

## Button map

This map is the same one given in [01 §3](01-spec-review.md#button-map-pause-menu-scheme).
Actions fire when the button is pressed. "Hold" means held for 2 s, timed from the press.

| State | Gray | Yellow | Green | Blue | Red |
|---|---|---|---|---|---|
| READY | — | settings | — | — | start (P1) |
| RUNNING | P1 | P2 | P3 | P4 | pause |
| PAUSED | — | settings | — | hold: end game | resume |
| SETTINGS | row up | value − | done | value + | row down |
| GAME OVER | view P1 | view P2 | view P3 | view P4 / hold: new game | — |

Holding red for 6 s powers the board off from any state.

---

## Screens

The LCD is 320×240, landscape. The background is black and text is white. Red (`0xF800`)
marks an over-limit turn. Text sizes are the BSP font scales: scale 2 is 12×16 px per
character, scale 3 is 18×24 px. The big digits are drawn by the app at scale 8, which is
48×64 px per character.

### S1 — Player clock

```
+--------------------------------------------------+
| GAME 0:42:17                         [RUNNING]   |  top bar, scale 2
+------------+-------------------------------------+
|  +------+  | P2 Alex                             |  scale 3
|  |      |  | Eyrie                               |  scale 2
|  | icon |  | TOTAL 0:12:31                       |  scale 2
|  | 96px |  | TURN 5   LIM 2:00                   |  scale 2
|  +------+  |                                     |
+------------+-------------------------------------+
|                                                  |
|                 0 1 : 2 3                        |  big turn timer, white -> red
|                                                  |
+--------------------------------------------------+
| [R] pause                                        |  hint row: colour swatch + label
+--------------------------------------------------+
```

| Region | x, y, w, h | Content |
|---|---|---|
| Top bar | 0, 0, 320, 24 | `GAME H:MM:SS` on the left. A state badge on the right: READY, RUNNING (green), PAUSED (yellow) or GAME OVER (red). |
| Icon tile | 8, 30, 96, 96 | The faction icon on a light tile. |
| Name | 116, 30 | `P<seat> <nickname>`, scale 3 |
| Faction | 116, 60 | The faction's short name, scale 2 |
| Total | 116, 82 | `TOTAL H:MM:SS`, scale 2 |
| Turn info | 116, 104 | `TURN <n>  LIM <m:ss>`, or `LIM off`, scale 2 |
| Big timer | 40, 140, 240, 64 | `MM:SS` at scale 8. From 100 minutes it becomes `H:MM:SS` at scale 6 (252 px wide). |
| Hint row | 0, 220, 320, 20 | One colour swatch and a label for each button that is live in this state. |

How S1 looks in each state:

| State | Shown player | Big timer | Hint row |
|---|---|---|---|
| READY | P1, the player who will start | `00:00`, white | `[R] start  [Y] settings` |
| RUNNING | The active player | That player's current turn, counting; red once past the limit | `[R] pause` |
| PAUSED | The active player | Frozen and drawn dim gray | `[R] resume  [Y] settings  [B] hold: end` |
| GAME OVER | The last active player, or the one picked with a player button | That player's **total**, frozen | `[Gy][Y][G][B] view  [B] hold: new game` |

While blue is being held toward a hold action, the hint row reads `keep holding...`. Letting
go before 2 s cancels.

### S2 — Settings

```
+--------------------------------------------------+
| SETTINGS                                         |  title bar, scale 2
+-------------------------------+------------------+
|   LIMIT 2:00                  |   +----------+   |
| > P1 <Jacob>                  |   |  icon of |   |  selected row: highlighted,
|   P1 Marquise                 |   |  selected|   |  value in < >
|   P2 Alex                     |   |  seat    |   |
|   P2 Eyrie                    |   +----------+   |
|   P3 Ahmed                    |     Jacob        |  preview panel
|   P3 Woodland                 |                  |
|   P4 Lorenzo                  |                  |
|   P4 Vagabond                 |                  |
+-------------------------------+------------------+
| [Gy] up [R] dn [Y] - [B] + [G] done              |  hint row
+--------------------------------------------------+
```

| Region | x, y, w, h | Content |
|---|---|---|
| Title bar | 0, 0, 320, 24 | `SETTINGS` |
| Row list | 0, 28, 204, 189 | 9 rows, 21 px apart, scale 2. The selected row is highlighted, starts with `>`, and shows its value as `<value>`. |
| Preview | 212, 28, 104, 189 | The selected seat's faction icon (96×96) with its nickname underneath. When the LIMIT row is selected it shows the limit explanation instead. |
| Hint row | 0, 220, 320, 20 | The arrow-pad labels |

What each row cycles through (values wrap around):

- **LIMIT:** Off, 0:30, 1:00, 1:30, 2:00, 3:00, 4:00, 5:00, 7:00, 10:00, 15:00.
- **Nickname rows:** Jacob, Alex, Ahmed, Lorenzo. A name already held by another seat is
  swapped with that seat.
- **Faction rows:** Marquise, Eyrie, Woodland, Vagabond, Riverfolk, Lizard Cult, Duchy,
  Corvids, Hundreds, Keepers, Twilight, Knaves, Lilypad.

Green returns to the state S2 was opened from (READY or PAUSED). Changes take effect
immediately.

---

## Acceptance tests

Run these on the board after `fw run rootclock`, with `fw console display` open in a second
terminal for AT-11. Each test is one step of a continuous demo, in order.

| AT | Steps | Expected | Covers |
|---|---|---|---|
| AT-01 | Flash, and wait for boot. | S1 shows READY: `GAME 0:00:00`, P1 Jacob, the Marquise icon, `00:00` in white, `TOTAL 0:00:00`, and the hint row `[R] start  [Y] settings`. | F11, F13 |
| AT-02 | Yellow, then red ×4 to reach `P2 Eyrie`, then blue until it reads `Lizard Cult`. | S2 opens, the cursor moves, the value cycles, and the preview shows the Lizard Cult icon. | F4, F9, F12 |
| AT-03 | Gray ×3 to reach `P1 Jacob`, then blue once. | P1 becomes `Alex` and P2 becomes `Jacob` (swap). | F10 |
| AT-04 | Gray to reach LIMIT, then blue until it reads `0:30`. Then green. | Back to READY. The turn-info line reads `LIM 0:30`. | F8, F12 |
| AT-05 | Red. | RUNNING. P1's big timer counts up from `00:00`, and GAME counts up with it. | F2, F5, F7 |
| AT-06 | At ~0:10 press gray (P1's own button). | Nothing resets. The timer keeps counting. | F1 |
| AT-07 | At ~0:15 press yellow. | S1 switches to P2 (now named Jacob) with the Lizard Cult icon. The big timer restarts at `00:00`. | F1, F5, F9 |
| AT-08 | Wait past 0:30 on P2's turn. | At `00:30`, one beep. The digits turn red and keep counting (`00:31`, `00:32`, ...). | F8 |
| AT-09 | Press gray. | P1's card shows `00:00` in white. `TOTAL` shows ~0:15 and grows. | F5, F6, F8 |
| AT-10 | Red. Wait 10 s. Red again. | PAUSED: all three numbers stay frozen for the 10 s and the hint row shows resume/settings/end. After resume, the same turn continues from the frozen value. | F2, F7 |
| AT-11 | Press green (P3), wait a few seconds, then red to pause. Hold blue 2 s. | GAME OVER. GAME stops. The console prints one result JSON line ([04](04-future-esp32-integration.md) schema) with 3 players, no P4 entry, each `total_ms` equal to the sum of its `turns_ms`, and `global_ms` equal to the sum of the totals. | F3, F7, R-EXP-2 |
| AT-12 | Press gray, yellow, green and blue in turn. | Each shows that player's final card with their total in the big digits. Blue (P4, never played) shows `00:00`. | F3, F11 |
| AT-13 | Hold blue 2 s. | READY again. All clocks are zero and the settings from AT-02..04 are kept. | F3 |
| AT-14 | Red (start), then tap red quickly. | The game pauses. The LED bar flickers one red pixel and restores. The board stays on. | F13 |
| AT-15 | Hold red 6 s. | The LED-bar countdown fills and the board powers off. Holding gray powers it back on, to READY with default settings (expected in the MVP). | F13 |

### Verification hook

This is not a feature. When the game ends (AT-11), the display CPU prints the result JSON
once with `DIAG()`. Two reasons:

- the totals can be checked against a stopwatch without a debugger;
- the export payload contract is exercised from day one, so [04](04-future-esp32-integration.md)
  only adds transport.

There is a second hook for the autonomous build ([05](05-agent-implementation-plan.md),
Phase 3). The display app accepts `press`, `hold` and `state` commands on its USB console and
injects them with `fwog_buttons_inject()`. That lets `apps/rootclock/tools/hw_check.py` run
AT-01…AT-13 on the real board with nobody pressing buttons.

## Definition of done

- `fw build rootclock` succeeds with no new warnings in `apps/rootclock/`.
- `hw_check.py` passes AT-01…AT-13 on the board: states, seats, settings, timing and the
  result JSON.
- The checks that need a person are listed for the developer to confirm after the run:
  - layout, colours, icons and the red digits;
  - the beep (AT-08);
  - the LED flicker (AT-14);
  - power-off (AT-15). The agent never holds red, because nobody would be there to switch the
    board back on.
- `apps/rootclock/README.md` documents the button map and the AT script.
- The core logic (`apps/rootclock/core/`) contains no SDK includes. Its host tests pass if a
  host compiler is available; otherwise that is stated in the merge commit.
- Everything is merged to `main` as described in [05](05-agent-implementation-plan.md).
