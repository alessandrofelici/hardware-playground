# rootclock: a turn clock for Root

A chess-clock-style timer for 3–4 players. Each player has a button, and pressing it starts
that player's turn. The board shows their turn time in large digits, their running total and
their faction icon. A settings page sets a per-turn time limit, each player's nickname and
each player's faction.

The full specification is in [`documents/`](../../documents/README.md) at the repo root:
spec review, MVP and acceptance tests, object model, the future ESP32 export, and the build
plan.

## Build and flash

```
fw build rootclock
fw run rootclock          # flashes the MAIN CPU; it carries and installs the display image
```

Never copy `rootclock_display.uf2` to the board (see the repo README).

## Buttons

Actions fire when the button is **pressed**. "Hold" means held for 2 s. The bottom row of the
screen always shows what each button currently does.

| State | Gray | Yellow | Green | Blue | Red |
|---|---|---|---|---|---|
| READY | — | settings | — | — | **start** (P1) |
| RUNNING | P1 | P2 | P3 | P4 | **pause** |
| PAUSED | — | settings | — | hold: **end game** | **resume** |
| SETTINGS | row up | value − | done | value + | row down |
| GAME OVER | view P1 | view P2 | view P3 | view P4 · hold: **new game** | — |

- **Holding red for 6 s powers the board off** from any screen. Holding gray powers it back on.
- A quick red tap briefly lights the power-off countdown on the LED bar. That is the BSP's
  countdown starting and stopping, not a fault.

## Rules

- **Turn timer:** starts at `00:00` on every turn.
- **TOTAL:** keeps adding up across all of a player's turns.
- **GAME:** the sum of all players' totals.
- **Pause** freezes all three. Resume continues the same turn.
- **Time limit** (settings: Off, 0:30 … 15:00) applies **per turn**. When a turn crosses it,
  the board beeps once (two short pips) and the digits turn red. The timer **keeps counting**.
  The next turn starts white again.
- **Nicknames** are Jacob, Alex, Ahmed and Lorenzo, and each can be held by only one player.
  Picking a name that another player has swaps the two.
- **Factions** come from the 13 icons in `assets/factions/` and may repeat.
- A seat that never takes a turn is left out of the results.
- When the game ends, the result is printed once on the display CPU's USB console as a
  `RESULT {json}` line. The JSON follows payload contract v1, in
  [`documents/04`](../../documents/04-future-esp32-integration.md).
- Settings are **not saved** across power-off. This is an MVP limit.

## Test console and the unattended hardware check

The display CPU accepts a few commands on its USB console. Open it at **115200**; opening a
port at 1200 baud reboots that CPU into BOOTSEL.

| Command | Effect |
|---|---|
| `press <gray\|yellow\|green\|blue\|red>` | inject a 100 ms tap through the real debounce/power path |
| `hold <btn> <ms>` | inject a hold. Red is refused at 1000 ms or more. |
| `state` | one `STATE {json}` line with the model: state, seats, clocks, settings, beep count |
| `result` | re-print the last `RESULT {json}` |
| `reset` | a new READY game with default settings |

`tools/hw_check.py` uses these to run acceptance tests AT-01…AT-14 from
[`documents/02`](../../documents/02-mvp.md) in about a minute:

```
fw run rootclock
py apps/rootclock/tools/hw_check.py
```

Checks that need a person, after a run:

- [ ] Layout matches documents/02 S1/S2: the icon, name, faction, TOTAL, TURN/LIM line, big
      digits and hint row.
- [ ] The big digits turn **red** past the limit and are dim gray while paused.
- [ ] The beep is audible at the limit (AT-08).
- [ ] A quick red tap flickers the LED bar and restores it (AT-14).
- [ ] Holding red 6 s powers off. Holding gray powers back on to READY with default settings
      (AT-15).

## Host tests

`core/` is plain C11, so its tests run on the PC. On this machine Strawberry Perl's MinGW
gcc works (from Git Bash):

```
export PATH="/c/Strawberry/c/bin:$PATH"
cmake -S apps/rootclock/tests -B build/rootclock-tests -G Ninja \
      -DCMAKE_C_COMPILER=C:/Strawberry/c/bin/gcc.exe
cmake --build build/rootclock-tests && ctest --test-dir build/rootclock-tests
```

## Where it differs from documents/02

These are small layout choices made so that everything fits at 320 px:

- **PAUSED hint row** is drawn at text scale 1. Its three labels don't fit at scale 2.
- **"keep holding..."** row also shows a blue progress bar.
- **S2 selected row** has the `>` marker against the left edge, so `>P2 <Lizard Cult>` fits in
  17 columns.
- **State badges** are filled colour pills rather than coloured text.
- **Big-timer colon** is nudged right inside its own cell. The font's colon sits at the left
  of the glyph.

## Layout

| Path | What |
|---|---|
| `core/` | Game logic, pure C11 with no SDK, host-testable: state machine, clocks, settings, input mapping, result JSON |
| `display/main.c` | The loop that wires `core/` to the BSP, plus the test console |
| `display/ui_*.c`, `beeper.c` | S1 and S2 screens, the big-digit renderer, the limit beep |
| `display/assets/faction_icons.c` | **Generated** 96×96 RGB565 icons. Regenerate with `py apps/rootclock/tools/gen_faction_icons.py` |
| `assets/factions/*.png` | Source icons (Root art © Leder Games; private repo) |
| `tests/` | Host tests for `core/`: a standalone CMake project, no SDK (see below) |
| `tools/hw_check.py` | Unattended hardware acceptance check |
| `main/main.c` | Main CPU: watchdog kick and display update, nothing app-specific yet |
