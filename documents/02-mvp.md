# 02 — MVP: rootclock

The MVP is exactly what was requested, and nothing more. Every feature below traces to a
requirement ID in [01-spec-review.md](01-spec-review.md), and every acceptance test is a step
you can perform on the board to show the feature works.

## Goal

A FreeWili OG that keeps time for a 3–4 player board game:

- one green **next** button ends the current turn and starts the next player's (002; 001 had
  one button per player);
- red pauses and resumes the clock;
- each player's turn time, total time and faction icon are shown on their clock screen;
- a settings page sets a per-turn time limit, each player's faction and each player's
  nickname.

## In scope

| # | Feature | Requirement |
|---|---|---|
| F1 | Green passes the turn to the next player: P1 → P2 → P3 (→ P4) → P1 *(002; 001 had gray/yellow/green/blue for P1–P4)* | R-BTN-1 |
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
| F14 | *(002)* A PLAYERS setting, 3 or 4, sets the rotation; a seat outside it is dimmed in S2 and never plays | G8 |
| F15 | *(002)* S1 shows the shown player's average completed turn (`AVG/TURN`) | G19 |
| F16 | *(002)* While paused, green cycles through the player cards without touching any clock | G18 |
| F17 | *(003)* While paused, a battery icon with the charge and the estimated time left | G20 |
| F18 | *(004)* At GAME OVER, yellow uploads the result(s) through the Bottlenose WiFi module to root-metrics; nothing is lost on failure | R-EXP-1, G21 |

## Out of scope

- *(Built in 004, F18.)* ~~Any ESP32 or mobile export~~ — see [04](04-future-esp32-integration.md).
- Saving settings across power-off.
- LED-bar effects beyond the BSP's power-off countdown.
- A results or summary screen. GAME OVER reuses S1.
- Sound beyond the single time-limit beep.
- Undo, or editing turn times.

## Button map

This map is the same one given in [01 §3](01-spec-review.md#button-map-pause-menu-scheme).
Actions fire when the button is pressed. "Hold" means held for 2 s, timed from the press.

| State | Gray | Yellow | Green | Blue | Red |
|---|---|---|---|---|---|
| READY | — | settings | — | — | start (P1) |
| RUNNING | — | — | next player | — | pause |
| PAUSED | — | settings | view next card | hold: end game | resume |
| SETTINGS | row up | value − | done | value + | row down |
| GAME OVER | — | send (004) | view next card | hold: new game | — |

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
|  |      |  | AVG/TURN 01:40                      |  scale 2
|  | icon |  | TOTAL 0:12:31                       |  scale 2
|  | 96px |  | TURN 5   LIM 2:00                   |  scale 2
|  +------+  |                                     |
+------------+-------------------------------------+
|                                                  |
|                 0 1 : 2 3                        |  big turn timer, white -> red
|                                                  |
+--------------------------------------------------+
| [G] next  [R] pause                              |  hint row: colour swatch + label
+--------------------------------------------------+
```

| Region | x, y, w, h | Content |
|---|---|---|
| Top bar | 0, 0, 320, 24 | `GAME H:MM:SS` on the left. A state badge on the right: READY, RUNNING (green), PAUSED (yellow) or GAME OVER (red). |
| Icon tile | 8, 30, 96, 96 | The faction icon on a light tile. |
| Name | 116, 30 | `P<seat> <nickname>`, scale 3 |
| Average | 116, 60 | `AVG/TURN MM:SS`: the shown player's average completed turn, or `AVG/TURN --:--` before their first turn ends, scale 2 *(002; 001 showed the faction's short name here)* |
| Total | 116, 82 | `TOTAL H:MM:SS`, scale 2 |
| Turn info | 116, 104 | `TURN <n>  LIM <m:ss>`, or `LIM off`, scale 2 |
| Big timer | 40, 140, 240, 64 | `MM:SS` at scale 8. From 100 minutes it becomes `H:MM:SS` at scale 6 (252 px wide). |
| Upload | 4, 206, 312, 13 | *(004)* GAME OVER only: one scale-1 line saying what yellow will do or how the upload is going (`WiFi up. Yellow: send`, `1 game saved on WiFi module, uploading`, `Uploaded to root-metrics`, or what went wrong), coloured white / yellow / green / red. |
| Battery | 128, 206, 192, 13 | *(003)* PAUSED only, right-aligned: a 20×10 battery icon filled to the charge and a scale-1 label, `70% ~4h40m left`, `88% charging` or `on USB`. |
| Hint row | 0, 220, 320, 20 | One colour swatch and a label for each button that is live in this state. |

How S1 looks in each state:

| State | Shown player | Big timer | Hint row |
|---|---|---|---|
| READY | P1, the player who will start | `00:00`, white | `[R] start  [Y] settings` |
| RUNNING | The active player | That player's current turn, counting; red once past the limit | `[G] next  [R] pause` |
| PAUSED | The active player, then each next player as green is pressed | The active player's frozen turn; another player's card shows their **total**. Dim gray either way. | `[R] resume  [G] view  [Y] settings  [B] hold: end` (scale 1) |
| GAME OVER | The last active player, then each next player as green is pressed | That player's **total**, frozen | `[G] view  [B] hold: new game`; with a result waiting to upload, `[G] view  [Y] send  [B] hold: new game` (scale 1) |

While blue is being held toward a hold action, the hint row reads `keep holding...`. Letting
go before 2 s cancels.

### S2 — Settings

```
+--------------------------------------------------+
| SETTINGS                                         |  title bar, scale 2
+-------------------------------+------------------+
|   PLAYERS 4                   |                  |
|   LIMIT 2:00                  |   +----------+   |
| > P1 <Jacob>                  |   |  icon of |   |  selected row: highlighted,
|   P1 Marquise                 |   |  selected|   |  value in < >
|   P2 Alex                     |   |  seat    |   |
|   P2 Eyrie                    |   +----------+   |
|   P3 Ahmed                    |     Jacob        |  preview panel
|   P3 Woodland                 |                  |
|   P4 Lorenzo                  |                  |  (dim with PLAYERS 3)
|   P4 Vagabond                 |                  |
+-------------------------------+------------------+
| [Gy] up [R] dn [Y] - [B] + [G] done              |  hint row
+--------------------------------------------------+
```

| Region | x, y, w, h | Content |
|---|---|---|
| Title bar | 0, 0, 320, 24 | `SETTINGS` |
| Row list | 0, 28, 204, 190 | 10 rows, 19 px apart, scale 2 *(001: 9 rows, 21 px)*. The selected row is highlighted, starts with `>`, and shows its value as `<value>`. The rows of a seat outside the rotation (P4 with 3 players) are dim gray. |
| Preview | 212, 28, 104, 190 | The selected seat's faction icon (96×96) with its nickname underneath. The LIMIT row shows the limit explanation instead, and the PLAYERS row shows the rotation (`green: next turn` / `P1>P2>P3>P1` / `P4 sits out`). |
| Hint row | 0, 220, 320, 20 | The arrow-pad labels |

What each row cycles through (values wrap around):

- **PLAYERS:** 3, 4 (default 4).
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
| AT-01 | Flash, and wait for boot. | S1 shows READY: `GAME 0:00:00`, P1 Jacob, the Marquise icon, `AVG/TURN --:--`, `00:00` in white, `TOTAL 0:00:00`, and the hint row `[R] start  [Y] settings`. | F11, F13, F15 |
| AT-02 | Yellow, then red ×5 to reach `P2 Eyrie`, then blue until it reads `Lizard Cult`. | S2 opens on PLAYERS, the cursor moves, the value cycles, and the preview shows the Lizard Cult icon. | F4, F9, F12 |
| AT-03 | Gray ×3 to reach `P1 Jacob`, then blue once. | P1 becomes `Alex` and P2 becomes `Jacob` (swap). | F10 |
| AT-04 | Gray to reach LIMIT, then blue until it reads `0:30`. Gray to PLAYERS, blue once (3). Then green. | The P4 rows turn dim. Back to READY. The turn-info line reads `LIM 0:30`. | F8, F12, F14 |
| AT-05 | Red. | RUNNING. P1's big timer counts up from `00:00`, and GAME counts up with it. | F2, F5, F7 |
| AT-06 | At ~0:10 press gray, yellow and blue. | Nothing changes. P1's turn keeps counting. | F1 |
| AT-07 | At ~0:15 press green. | S1 switches to P2 (now named Jacob) with the Lizard Cult icon. The big timer restarts at `00:00`. P1's average is its one turn. | F1, F5, F9, F15 |
| AT-08 | Wait past 0:30 on P2's turn. | At `00:30`, one beep. The digits turn red and keep counting (`00:31`, `00:32`, ...). | F8 |
| AT-09 | Green, wait 1 s, green. | P3's turn, white, then P1's second turn: P4 is skipped with 3 players. P1's `TOTAL` resumes from ~0:15 and grows; its average does not move. | F1, F5, F6, F8, F14 |
| AT-10 | Red. Green ×3, wait, green, then red. | PAUSED: the battery line shows under the big digits. Green shows P2, P3, P1 in turn while every number stays frozen. Red resumes P1's same turn even though P2's card was showing. | F2, F7, F16, F17 |
| AT-11 | Green (P2), wait a second, red to pause, hold blue 2 s. | GAME OVER. GAME stops. The console prints one result JSON line ([04](04-future-esp32-integration.md) schema) with 3 players, no P4 entry, `app_version` `003`, each `total_ms` equal to the sum of its `turns_ms`, and `global_ms` equal to the sum of the totals. | F3, F7, R-EXP-2 |
| AT-12 | Green ×3, then gray and blue taps. | Green shows P3, P1, P2 with their totals in the big digits. The other taps change nothing. | F3, F11 |
| AT-12U | Yellow. | *(004)* With a Bottlenose running sketch 1.3: the line goes from `Saving to the WiFi module...` to `Uploaded to root-metrics`, and the game is on the root-metrics page. Without one: `No WiFi module answering`, and the result stays on the board (yellow retries). | F18 |
| AT-13 | Hold blue 2 s. | READY again. All clocks and averages are zero and the settings from AT-02..04 are kept (3 players). | F3 |
| AT-14 | Red (start), then tap red quickly. | The game pauses. The LED bar flickers one red pixel and restores. The board stays on. | F13 |
| AT-15 | Hold red 6 s. | The LED-bar countdown fills and the board powers off. Holding gray powers it back on, to READY with default settings (expected in the MVP). | F13 |

`hw_check.py` runs AT-01…AT-14 against the board, or against the simulator in
`apps/rootclock/tools/sim/` when no board is available.

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
