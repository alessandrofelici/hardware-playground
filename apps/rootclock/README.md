# rootclock: a turn clock for Root

A chess-clock-style timer for 3–4 players. Green ends the current turn and starts the next
player's. The board shows their turn time in large digits, their running total, their average
turn and their faction icon. A settings page sets the number of players, a per-turn time
limit, each player's nickname and each player's faction. After the game, yellow uploads the
result through a Bottlenose WiFi Orca to [root-metrics](https://github.com/alessandrofelici/root-metrics).

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
| GAME OVER | — | **send** (004) | view next card | hold: **new game** | — |

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
  [`documents/04`](../../documents/04-future-esp32-integration.md). It is also kept for
  upload (below).
- Settings are **not saved** across power-off. This is an MVP limit.

## Uploading results (004)

With a Bottlenose WiFi Orca plugged into the breakout header, finished games go to
root-metrics (hosted on Vercel, or Docker on your network):

1. Set root-metrics up and give it an `INGEST_TOKEN` (its README: *Hosting on Vercel +
   Supabase*, or *Docker*).
2. Flash the Bottlenose with `apps/bottlenose/c6/bottlenose_c6` **1.4 or later** (1.3 drops
   every game the board sends it). In its `secrets.h` put your Wi-Fi, `RESULTS_URL`
   (`https://<app>.vercel.app/api/results`, the Vercel app, not the Supabase project URL) and
   `RESULTS_TOKEN` (the same token). See `apps/bottlenose/README.md`.
3. `fw run rootclock`.

Then, at GAME OVER:

- the board switches the WiFi module on and the line under the big digits says how it is
  going: `WiFi connecting...`, then `WiFi up. Yellow: send`;
- **yellow** sends the game: `Saving to the WiFi module...`, then
  `1 game saved on WiFi module, uploading`, then `Uploaded to root-metrics` in green;
- the game is on the root-metrics page.

Nothing is lost when something is missing. The display keeps up to 4 finished games until the
WiFi module confirms it saved them to its own flash (they survive starting a new game, not a
power-off); the module keeps them through power-offs and retries until the server has them.
If there is no Bottlenose, or it does not answer, the line says so and yellow tries again
later. During play the module's radio is off, to save battery.

Without a Bottlenose, a computer on the display's USB console can do the upload instead:

```
py apps/rootclock/tools/upload_results.py --url https://<app>.vercel.app/api/results --token <token>
py apps/rootclock/tools/upload_results.py --url ... --token ... --watch   # every game as it ends
```

The main CPU's console (`fw console main`) shows the conversation with the module: `[c6] ...`
for what it says, `[to c6] ...` for what is sent (`!verbose` adds every data line).

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
| `state` | one `STATE {json}` line with the model: state, seats, clocks, averages (`avg`, -1 = none yet), settings (`players`, `limit_ms`, `nick`, `fac`), beep count, battery (`batt`: mode, mV, percent, minutes left or -1, measured), upload (`uplink`: link, main heard, C6 alive, wifi, result state, http, games waiting on the C6, games in the display's outbox, send phase) |
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

`shots.py` plays a short game and saves every screen as a PNG plus `sheet.png`. The main CPU's
relay (`main/bridge.c`) runs inside the simulator too, and `tools/sim/fake_c6.py` plays the
WiFi module: with `--url` and `--token` it really uploads, so a game can go all the way to a
running root-metrics. The same `hw_check.py` runs against it through a pseudo-terminal:

```
SIM_PTY=/tmp/rootclock-tty \
SIM_C6="python3 apps/rootclock/tools/sim/fake_c6.py --url http://127.0.0.1:8080/api/results --token dev" \
  build/rootclock-sim/rootclock_sim &
python3 apps/rootclock/tools/hw_check.py --port /tmp/rootclock-tty
```

Leave `SIM_C6` out to simulate a board with no Bottlenose.

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
| `display/export.c`, `common/rc_link.h` | The upload's display half: outbox, WiFi on/off, sending on yellow |
| `core/rc_uplink.c` | Upload message formats and state machines (pure, host-tested) |
| `main/bridge.c` | The main CPU's relay to the WiFi module (portable; the simulator runs it too) |
| `tools/upload_results.py` | Upload from a computer on the USB console, without a Bottlenose |
| `display/main.c` | The loop that wires `core/` to the BSP, plus the test console |
| `display/ui_*.c`, `beeper.c` | S1 and S2 screens, the big-digit renderer, the limit beep |
| `display/assets/faction_icons.c` | **Generated** 96×96 RGB565 icons. Regenerate with `py apps/rootclock/tools/gen_faction_icons.py` |
| `assets/factions/*.png` | Source icons (Root art © Leder Games; private repo) |
| `tests/` | Host tests for `core/`: a standalone CMake project, no SDK (see below) |
| `tools/hw_check.py` | Unattended hardware acceptance check |
| `tools/sim/` | The display app built for the PC: screenshots and `hw_check.py` with no board |
| `main/main.c` | Main CPU: watchdog, display update, UART1 to the WiFi module, the link, its USB console |
