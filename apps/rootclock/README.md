# rootclock: a turn clock for Root

A chess-clock-style timer for 3–4 players. Green ends the current turn and starts the next
player's. The board shows their turn time in large digits, their running total, their average
turn and their faction icon. A settings page sets the number of players, a per-turn time
limit, each player's nickname and each player's faction.

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
| RUNNING | — | — | **next** player | — | **pause** |
| PAUSED | — | settings | view next card | hold: **end game** | **resume** |
| SETTINGS | row up | value − | done | value + | row down |
| GAME OVER | — | — | view next card | hold: **new game** | — |

Since version 002, green is the only turn button: P1 → P2 → P3 (→ P4) → P1. While paused or
after the game it cycles the player cards instead, without touching any clock; red resumes
the paused turn whichever card is showing.

- **Holding red for 6 s powers the board off** from any screen. Holding gray powers it back on.
- A quick red tap briefly lights the power-off countdown on the LED bar. That is the BSP's
  countdown starting and stopping, not a fault.

## Rules

- **Turn timer:** starts at `00:00` on every turn.
- **TOTAL:** keeps adding up across all of a player's turns.
- **AVG/TURN:** the average of a player's completed turns. The running turn is left out, so
  it only changes when a turn ends; `--:--` until the first one does.
- **GAME:** the sum of all players' totals.
- **Pause** freezes all three. Resume continues the same turn.
- **Time limit** (settings: Off, 0:30 … 15:00) applies **per turn**. When a turn crosses it,
  the board beeps once (two short pips) and the digits turn red. The timer **keeps counting**.
  The next turn starts white again.
- **Players** (settings, 3 or 4, default 4) is how many seats green rotates through. With 3,
  P4's rows are dim in settings and P4 never takes a turn.
- **Nicknames** are Jacob, Alex, Ahmed and Lorenzo, and each can be held by only one player.
  Picking a name that another player has swaps the two.
- **Factions** come from the 13 icons in `assets/factions/` and may repeat.
- A seat that never takes a turn is left out of the results.
- While paused, the active player's card shows their frozen turn; any other card shows that
  player's total. Both are dim gray.
- **Battery** (003): while paused, a small battery icon and a label sit under the big digits,
  right-aligned: `70% ~4h40m left`, `88% charging` or `on USB`. See below for how it is
  worked out.
- When the game ends, the result is printed once on the display CPU's USB console as a
  `RESULT {json}` line. The JSON follows payload contract v1, in
  [`documents/04`](../../documents/04-future-esp32-integration.md).
- Settings are **not saved** across power-off. This is an MVP limit.

## Battery estimate

The board has no fuel gauge, only the BQ25896 charger, which measures the battery voltage in
20 mV steps and reports USB input and charge state. `display/battery.c` reads three charger
registers every 5 s (status, BATV, VBUSV; it only turns on the charger's ADC and never writes
a charge setting) and feeds `core/rc_battery.c`:

- **Percent** comes from the smoothed voltage through a typical 1-cell Li-ion curve
  (3.27 V = 0 %, 4.20 V = 100 %). It reads a little low under load and high while charging.
- **Time left** is percent divided by the drain rate. For the first 10 minutes on battery the
  rate is an assumption, 15 %/hour (about 150 mA from a 1000 mAh cell). After that it is
  measured: the slope of the last 30 minutes of readings. Plugging or unplugging USB starts
  the measurement again. `STATE` reports which one is in use (`batt.measured`).

The 15 %/hour assumption and the voltage curve are guesses until checked against this board's
cell; both are constants at the top of `core/rc_battery.{h,c}`.

## Test console and the unattended hardware check

The display CPU accepts a few commands on its USB console. Open it at **115200**; opening a
port at 1200 baud reboots that CPU into BOOTSEL.

| Command | Effect |
|---|---|
| `press <gray\|yellow\|green\|blue\|red>` | inject a 100 ms tap through the real debounce/power path |
| `hold <btn> <ms>` | inject a hold. Red is refused at 1000 ms or more. |
| `state` | one `STATE {json}` line with the model: state, seats, clocks, averages (`avg`, -1 = none yet), settings (`players`, `limit_ms`, `nick`, `fac`), beep count, battery (`batt`: mode, mV, percent, minutes left or -1, measured) |
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

## Without a board: the simulator

`tools/sim/` builds the display app for the PC: `display/main.c`, both screens and `core/`,
unchanged, against a stand-in for the BSP (`tools/sim/include/fwog_display.h`). The LCD is a
framebuffer, the buttons go through the BSP's real debouncer, and the test console is
stdin/stdout or a pseudo-terminal. Linux or macOS:

```
cmake -S apps/rootclock/tools/sim -B build/rootclock-sim -G Ninja -DCMAKE_C_COMPILER=gcc
cmake --build build/rootclock-sim
python3 apps/rootclock/tools/sim/shots.py build/rootclock-sim/rootclock_sim build/rootclock-shots
```

`shots.py` plays a short game and saves every screen as a PNG plus `sheet.png`. The same
`hw_check.py` runs against it through a pseudo-terminal:

```
SIM_PTY=/tmp/rootclock-tty build/rootclock-sim/rootclock_sim &
python3 apps/rootclock/tools/hw_check.py --port /tmp/rootclock-tty
```

It cannot show the panel's real colours and refresh, the beep, the LED bar or power-off, so
it does not replace a run on the board.

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
| `core/` | Game logic, pure C11 with no SDK, host-testable: state machine, clocks, settings, input mapping, result JSON, battery estimate |
| `display/battery.c` | Reads the charger into the battery estimate |
| `display/main.c` | The loop that wires `core/` to the BSP, plus the test console |
| `display/ui_*.c`, `beeper.c` | S1 and S2 screens, the big-digit renderer, the limit beep |
| `display/assets/faction_icons.c` | **Generated** 96×96 RGB565 icons. Regenerate with `py apps/rootclock/tools/gen_faction_icons.py` |
| `assets/factions/*.png` | Source icons (Root art © Leder Games; private repo) |
| `tests/` | Host tests for `core/`: a standalone CMake project, no SDK (see below) |
| `tools/hw_check.py` | Unattended hardware acceptance check |
| `tools/sim/` | The display app built for the PC: screenshots and `hw_check.py` with no board |
| `main/main.c` | Main CPU: watchdog kick and display update, nothing app-specific yet |
