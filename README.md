# hardware-playground

Firmware apps for the **FreeWili OG**, built on Intrepid's
[FreeWili OG board support package](https://github.com/freewili/wiliOGbsp)
(git submodule in `external/wiliOGbsp`).

The board has **two RP2040s**:
- the **display CPU** runs the LCD, the 5 buttons, the 7 RGB LEDs, IR, the mic, the speaker, the accelerometer and the RTC;
- the **main CPU** runs the two CC1101 sub-GHz radios, the iCE40 FPGA and the breakout I/O.

Every app has a half for each CPU. Building an app produces one file,
`build/apps/<app>/<app>_main.uf2`, that flashes **both**: it goes to the main
CPU, which sends the embedded display image to the display CPU over the
inter-CPU link. To switch apps, you flash a different file.

## One-time setup

1. **Toolchain.** In VS Code, the Raspberry Pi Pico extension installs it under
   `~/.pico-sdk` (SDK 2.3.1, toolchain 15_2_Rel1, ninja v1.13.2, cmake v4.3.4).
   The quickest way is to create any C project with it once.
2. **Python 3** with `pyserial`: `py -m pip install pyserial`.
3. **BSP:** `git submodule update --init`.
4. **Board, once:** install the OG display bootloader with the
   [FreeWili OG App Explorer](https://github.com/freewili/fwOGAppExplorer/releases/latest)
   (**OG Bootloader Installer** tab). It erases the main CPU.

## Daily use

`fw` is `tools/fw.py`. In **PowerShell** type `fw.cmd`, because `fw` is a
built-in PowerShell alias for `Format-Wide`. `fw.cmd` works directly in the VS
Code terminal, which has `tools/` on PATH. Anywhere else, run
`.\tools\fw.cmd` or `py tools/fw.py`.

| Do | Command |
|---|---|
| See apps (`*` = current) | `fw list` |
| Switch the current app | `fw use <app>` |
| Build | `fw build [app]` |
| Flash | `fw flash [app]` |
| Build + flash | `fw run [app]` |
| New app from the template | `fw new-app <name>` |
| Back to stock FreeWili firmware | `fw stock` (downloads the latest release, verifies its SHA-256) |
| USB log console | `fw console main` / `fw console display` |
| Put one CPU into BOOTSEL | `fw bootsel main` / `fw bootsel display` |

`[app]` defaults to the current app. Flashing never needs a button: `fw`
identifies the main CPU over USB and reboots it into BOOTSEL itself.

### In VS Code

The Pico extension's **Compile** and **Run** status-bar buttons call the
`Compile Project` / `Run Project` tasks in `.vscode/tasks.json`, which are
`fw build` and `fw run` for the current app. Switch the current app with the task **fw: switch app**
(Terminal -> Run Task). The other `fw:` tasks cover the remaining commands.

Do **not** replace these tasks with the extension's defaults: those run
`picotool load -fx`, which cannot tell the board's two RP2040s apart.

## Writing an app

```
apps/<name>/
  CMakeLists.txt     declares <name>_display then <name>_main, each with VERSION + DESCRIPTION
  display/main.c     #include "fwog_display.h"
  main/main.c        #include "fwog_main.h"
```

`apps/hello` is a worked example (LCD text, buttons, LED bar). API reference:
the headers under `external/wiliOGbsp/bsp/`; pin map:
`external/wiliOGbsp/docs/hardware/pinmap.md`.

## Rules that protect the board

These come from the BSP's
[AGENTS.md](external/wiliOGbsp/AGENTS.md); read it before driver-level work.

- **Only ever flash the main CPU** (`fw flash` does exactly that). Never copy a
  display `.uf2` to the display CPU: it won't boot, and the display CPU has no
  BOOTSEL button.
- **One CPU in BOOTSEL at a time.** Both show up as an identical `RPI-RP2` drive.
- **Display apps** call `fwog_power_poll()` every loop iteration (red held 6 s =
  power off) and take buttons from its result.
- **Main apps** call `board_watchdog_kick()` every loop iteration, or the board
  resets every 8.3 s.
- **UART0 is the inter-CPU link.** Log with `DIAG()`, never `printf` to UART.
- Never pass `-DPICO_BOARD` to CMake.

**Recovery** if a main app hangs: hold **red** through a reset, and the main CPU
should drop into BOOTSEL. The BSP notes this was reasoned from the hardware
design, not measured. The App Explorer's **Recovery** tab covers the rest.
