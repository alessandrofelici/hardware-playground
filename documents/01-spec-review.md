# 01 — Spec review: rootclock

**rootclock** is a turn clock for 3–4 players of a board game (Root; the faction icons are
the 13 PNGs in `root icons/`). It runs on the FreeWili OG. It works like a chess clock: each
player has their own button, pressing it starts that player's turn, and the device tracks each
player's turn time, their total time and the global game time.

This document restates the request as numbered requirements and lists what the hardware
allows. It then records every gap or conflict that was found and how it was resolved, so the
MVP ([02](02-mvp.md)) and the object model ([03](03-object-model.md)) do not have to reopen
these questions.

| | |
|---|---|
| App folder | `apps/rootclock/` |
| Targets | `rootclock_display`, `rootclock_main` |
| USB product strings | `FWOG display rootclock 002`, `FWOG main rootclock 002` (001 was the first MVP) |
| Build / flash | `fw build rootclock`, `fw run rootclock` (never anything else; see `CLAUDE.md`) |

---

## 1. Requirements as stated

### Controls

| ID | Requirement |
|---|---|
| R-BTN-1 | A specific button switches the game clock to player 1, 2, 3 or 4. *(Amended in 002: one green **next** button passes the turn to the next player in seat order.)* |
| R-BTN-2 | Another button pauses and starts the clock. |
| R-BTN-3 | One button ends the global timer, which ends the game. |
| R-BTN-4 | One button opens settings. |

### Settings

| ID | Requirement |
|---|---|
| R-SET-1 | A time limit can be set. At the limit the FreeWili makes a noise and the timer text turns from white to red. |
| R-SET-2 | Each player can assign a faction icon, shown on their timer screen. |
| R-SET-3 | Each player picks a nickname from a fixed list: Jacob, Alex, Ahmed, Lorenzo. |

### Players and timers

| ID | Requirement |
|---|---|
| R-PLY-1 | There are several players (3–4). |
| R-TMR-1 | Each player has an individual timer. |
| R-TMR-2 | That timer starts at 0 on the button press, every time it is their turn. |
| R-TMR-3 | Each player has a total-time value that keeps accumulating. |
| R-TMR-4 | There is a global timer. |

### Screens

| ID | Requirement |
|---|---|
| R-SCR-1 | A per-player clock screen shows the turn timer large, the player's total time smaller, and their faction icon. |
| R-SCR-2 | There is a settings page. |

### Later: the mobile app

| ID | Requirement |
|---|---|
| R-EXP-1 | *(Not in the MVP)* After the game, send each player's nickname, faction, total time and the time of every turn to a mobile app through the ESP32 (the Bottlenose WiFi Orca). |
| R-EXP-2 | The design now must let that be added later without rework. Concretely: the game data is kept in a form that can be serialised, and a payload contract exists ([04](04-future-esp32-integration.md)). |

### Requirements the BSP imposes on every app

These are not negotiable (`external/wiliOGbsp/AGENTS.md`).

| ID | Requirement |
|---|---|
| R-SYS-1 | Red held 6 s powers the board off. The display app declares `FWOG_POWER_DEFAULT()` and calls `fwog_power_poll(now_ms)` every loop iteration, taking its buttons from that call's result. |
| R-SYS-2 | The main app declares `FWOG_WATCHDOG_DEFAULT()` and calls `board_watchdog_kick()` every loop iteration. |
| R-SYS-3 | Both halves declare a three-digit `VERSION` and a `DESCRIPTION`. |
| R-SYS-4 | Flash only with `fw flash` / `fw run`. Logging uses `DIAG()`, never `printf` to UART0. |

---

## 2. Hardware constraints that shape the design

| Area | Fact (source) | Consequence |
|---|---|---|
| Buttons | Five buttons: gray, yellow, green, blue, red (`bsp/display_cpu/input/buttons.h`). `fwog_power_poll()` returns `down` / `pressed` / `released` bitmasks. Long-press and double-click are left to the app. | Seven actions have to share five buttons, see §3. The app does its own hold timing by timestamping `pressed` edges. |
| Red button | Red's edges reach the app, so a red tap is usable. Any red press arms the 6 s power-off countdown, which draws on the LED bar and restores the previous colours on release. Holding red for 6 s always powers the board off. | Red can be "pause". A short tap flashes one red LED, which is cosmetic. No action may require holding red. |
| LCD | 320×240 landscape, RGB565, drawn directly with no framebuffer (`bsp/display_cpu/lcd/st7789.h`). | Redraw only the regions that changed. A full-screen redraw takes about 25 ms and tears. |
| Text | One 6×8 font. `lcd_text_draw()` draws nothing above scale 3, so the largest text is 18×24 px. | The big turn timer needs its own renderer. `lcd_font_expand()` accepts any scale, so the app expands glyphs at scale 8 (48×64 px) into a static buffer and blits them. |
| Sound | There is no beep API. `i2s_audio_start()` plays a static sample buffer at a fixed 8 kHz and does not block. `i2s_audio_process()` must be called every loop. | The app synthesises a short ~1 kHz tone once into a static buffer, following `external/wiliOGbsp/apps/bench/display/main.c`. |
| Storage | The display CPU has no settings or flash-storage API. | Settings reset at power-off in the MVP. Persisting them is post-MVP (last 4 KB flash sector, with a magic number and CRC). |
| Icons | 13 RGBA PNGs, 150–656 px, with real transparency and varying margins. One filename contains spaces. | A Pillow generator crops each to its alpha bounding box and fits it to 96×96. It composites onto a light tile so the black line art stays visible on the dark UI, then emits RGB565 C arrays. About 18 KB of flash each, ~240 KB in total, blitted straight from flash. |
| Inter-CPU link | UART0 framed messages, payload ≤ 4160 B. App message types start at 0x40. RTS/CTS can block forever (`apps/bottlenose/common/bn_link.h`). | Not used in the MVP. The future export uses a timeout send, as bottlenose does. |
| ESP32-C6 | Connected to main-CPU UART1 at 115200 8N1. The bottlenose sketch speaks a custom ASCII `KEY value` line protocol, and its inbound lines are capped at 80 chars. | Future export: the main CPU relays the result to the C6 in short lines. See [04](04-future-esp32-integration.md). |

---

## 3. Gaps and conflicts, and how they were resolved

**Status key**

- **User:** decided by the user while this was being planned.
- **Assumed:** a default chosen here. The user accepted all of them on 2026-11-20.
- **Technical:** forced by the hardware or the BSP.

| # | Gap or conflict | Resolution | Status |
|---|---|---|---|
| G1 | Seven actions (P1–P4, pause/start, end, settings) but only five buttons. | **Pause-menu scheme**, see the button map below. | User |
| G2 | Red is also the power button. | A red tap pauses. Holding red 6 s powers off from any screen. Nothing requires holding red. | Technical |
| G3 | "Time limit" could mean per turn or per total. | **Per turn.** At the limit: one beep, and the big turn digits turn red. **The timer keeps counting** past the limit. | User |
| G4 | Does the global timer run while paused? | **No.** Pause freezes the turn timer, the player's total and the global timer, so global time = play time = Σ player totals. | User |
| G5 | What "start" means. | At power-on the app is READY with all clocks at zero. Red starts the game with **player 1's** turn. | Assumed |
| G6 | Pressing the button of the player who is already active. | Does nothing, so a turn cannot be reset by accident. *002: moot, there are no per-player buttons; green always passes to a different seat.* | Assumed |
| G7 | Pausing in the middle of a turn. | Resume continues the **same** turn. It does not start a new turn entry. | Assumed |
| G8 | 3 vs 4 players. | *002:* a **PLAYERS** setting (3 or 4, default 4) sets how many seats green rotates through. A seat outside the rotation never takes a turn, so it is absent from the results. | User (002) |
| G9 | Two players with the same nickname. | Nicknames are unique. Choosing a name another seat holds **swaps** the two. Defaults: P1 Jacob, P2 Alex, P3 Ahmed, P4 Lorenzo. | Assumed |
| G10 | Which factions are offered, and must they be unique. | The 13 supplied icons. Not unique, since Root allows two Vagabonds. Defaults: P1 Marquise, P2 Eyrie, P3 Woodland, P4 Vagabond. | Assumed |
| G11 | What happens after the game ends. | GAME OVER: the clocks freeze. Green cycles through each player's final card (002; in 001 each player button showed its own). Holding blue 2 s starts a new game, keeping the settings. | Assumed |
| G12 | When settings can be opened. | Only from READY or PAUSED, never while a clock is running. This follows from G1. | User (via G1) |
| G13 | The time limit is changed in the middle of a turn. | If the current turn is already past the new limit, the digits turn red immediately, without a beep. | Assumed |
| G14 | A turn longer than 99:59. | The big timer switches from `MM:SS` (scale 8) to `H:MM:SS` (scale 6). | Technical |
| G15 | Settings persistence. | Not in the MVP. Settings return to their defaults after power-off. | Assumed |
| G16 | Very long games. | At most 128 recorded turns per player. Further turns are added to the last slot and `turns_truncated` is set, so total = Σ turns still holds. | Assumed |
| G17 | Large digits, beep and icons are not provided by the BSP. | App-side renderer, tone synthesis and icon generator (§2). | Technical |
| G18 | *002:* five buttons for four players was crowded, and a wrong player button ended the wrong turn. | **One next button.** Green ends the current turn and starts the next seat's (P1 → P2 → P3 (→ P4) → P1). While paused or after the game, green cycles the player cards instead; it only changes what is shown. | User (002) |
| G19 | *002:* what replaces the faction line on S1. | **AVG/TURN**: the average of the shown player's completed turns. The running turn is left out so the number only moves when a turn ends. The icon still shows the faction. | User (002) |

### Button map (pause-menu scheme, amended in 002)

Actions fire on the **press** edge, so a turn switch is timestamped as early as possible
(about 5 ms of debounce latency). A "hold" is measured from that same press edge.

| State | Gray | Yellow | Green | Blue | Red |
|---|---|---|---|---|---|
| READY | — | open settings | — | — | **start** (P1's turn) |
| RUNNING | — | — | **next** player | — | **pause** |
| PAUSED | — | open settings | view next card | hold 2 s: **end game** | **resume** |
| SETTINGS | row up | value − | done | value + | row down |
| GAME OVER | — | — | view next card | hold 2 s: **new game** | — |
| *any state* | | | | | *held 6 s: power off* |

001 had one button per player while RUNNING (gray P1, yellow P2, green P3, blue P4) and the
same four buttons viewed each card in GAME OVER. 002 replaced both with green.

- In SETTINGS the row-and-value keys follow the BSP's recommended arrow-pad layout: green is
  select/OK, yellow left, blue right, gray up, red down.
- The labels for whatever the buttons currently do are always shown on screen as coloured
  swatches in the bottom hint row.

---

## 4. Timer semantics (normative)

1. **Turn timer.** It starts at `00:00` when a player's turn begins, either by green (next) or
   by START for P1, and counts up while RUNNING.
2. **Player total.** This is the sum of the player's completed turns plus the running turn.
   It never resets during a game.
3. **Global timer.** It counts whenever any turn is running. It is derived from the same
   timestamps as the turns, so `global == Σ totals` exactly, to the millisecond.
4. **Turn record.** When a turn ends, its duration (paused time excluded) is appended to that
   player's turn log. A turn ends when green passes it to the next player or the game ends.
5. **Pause.** It freezes all three timers. Resume continues the same turn.
6. **Time limit.** It is per turn and set in settings: Off, 0:30, 1:00, 1:30, 2:00, 3:00, 4:00,
   5:00, 7:00, 10:00 or 15:00.
   - When a running turn crosses the limit it beeps once (two short 1 kHz pips), and the big
     digits turn red and stay red for the rest of that turn.
   - The digits keep counting past the limit.
   - The next turn starts white again.
7. **Clock source.** `to_ms_since_boot()`, a monotonic uint32 in ms. All comparisons use
   unsigned subtraction, which is safe across wrap for any game shorter than 24 days.

---

## 5. Risks

| Risk | Mitigation |
|---|---|
| The big-digit redraw tears or flickers (the panel's TE pin is unused). | Redraw only the glyphs that changed, about once a second. Never clear the full screen during play. |
| A red tap flashes the LED-bar power countdown. | This is cosmetic and the bar restores itself. Documented in the README. |
| Settings are lost at power-off. | Accepted for the MVP. Persistence is post-MVP (§3, G15). |
| Icon licensing: Root artwork is © Leder Games. | Fine for personal firmware. Check whether the git remote is public before pushing the PNGs or the generated arrays. |
| Host tests need a host C compiler, and MSYS2 gcc (the BSP's expected path) is absent. | **Resolved during the build:** Strawberry Perl's MinGW gcc 13.2 was already installed at `C:/Strawberry/c/bin/gcc.exe`, and the core host tests run with it (6/6 pass). Nothing was installed. The command is in `apps/rootclock/README.md`. |
| One physical board shared by several agents. | Only the lead agent flashes, and only with the user's go-ahead ([05](05-agent-implementation-plan.md)). |
| A display-only UF2 copy would brick the display CPU. | Only `fw run rootclock` / `fw flash rootclock` is used, and they flash main only. |
