# bottlenose: prove the Bottlenose WiFi Orca works

The Bottlenose WiFi Orca (rev 1, 5/30/2025) carries an ESP32-C6-MINI-1 and
plugs into the FreeWili OG's 20-pin breakout header. This app makes the C6 join
your Wi-Fi, sync the time over NTP and fetch one URL over HTTP, then shows the
result on the OG's LCD.

Since sketch 1.3 the same C6 firmware also uploads finished **rootclock** games to
root-metrics (see [Uploading rootclock games](#uploading-rootclock-games)). The bottlenose app
works as before with it.

There are three parts:

| Part | Runs on | Built with |
|---|---|---|
| `main/` | OG main CPU: bridges the C6 to USB and to the display | `fw build bottlenose` |
| `display/` | OG display CPU: status screen, log, LED bar, buttons | (embedded in the main image) |
| `c6/bottlenose_c6/` | the Bottlenose's ESP32-C6 | Arduino IDE |

## Wiring (nothing to wire: it plugs in)

| ESP32-C6 | Header pin | OG main CPU |
|---|---|---|
| GPIO16 (UART0 TX) | 5 | GPIO 9, UART1 RX |
| GPIO17 (UART0 RX) | 9 | GPIO 8, UART1 TX |

115200 baud, 8N1, no flow control. FreeWili publishes no Bottlenose pinout.
This mapping comes from a community project built on the same BSP,
[nullswitch23/fwog_defcon](https://github.com/nullswitch23/fwog_defcon)
(`firmware/bottlenose/README.md`). That README also says the Bottlenose is
powered from the OG header and its USB-C port is for flashing and debugging
only, so keep the OG switched on while using it.

## Step 1: flash the OG and check the link (no C6 code needed)

```
fw run bottlenose
fw console main
```

The console says which display result it got and repeats `nothing from the C6
yet` every 10 s until the C6 says something. Press **RESET** on the
Bottlenose: GPIO16/17 are also the C6's boot UART, so its ROM boot banner
(`ESP-ROM:esp32c6-...`) should appear as `[c6] ...` lines, and in the log at
the bottom of the LCD. If it does, the header, the level shifters and UART1
all work.

On a normal boot the stock firmware prints nothing, so RESET alone may show
nothing. The ROM always prints when the C6 enters download mode: hold BOOT,
tap RESET, release BOOT, and `waiting for download` should appear. Uploading
a sketch (step 2) does the same thing, so its banner shows up here too.

The stock firmware is FreeWili's `Wet_WILi` (ESP-IDF v5.4). Its protocol is
not published, so this app just logs whatever it says. To keep a copy before
step 2, with `esptool` from the esp32 core:

```
esptool --port COMx read-flash 0 ALL firmware/stock/bottlenose-c6-stock.bin
```

`firmware/stock/` is git-ignored. Writing the file back with
`esptool --port COMx write-flash 0 <file>` restores the stock firmware.

## Step 2: flash the C6

This replaces the Bottlenose's stock firmware.

1. Arduino IDE: add the Espressif boards URL
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
   (File > Preferences), then install **esp32 by Espressif Systems**, version
   3.x, from the Boards Manager.
2. Copy `c6/bottlenose_c6/secrets.example.h` to `secrets.h` in the same
   folder and set your network. The C6 is 2.4 GHz only. `secrets.h` is
   git-ignored. For rootclock uploads also set `RESULTS_URL` and
   `RESULTS_TOKEN` (below).
3. Open `c6/bottlenose_c6/bottlenose_c6.ino`. Tools: **Board: ESP32C6 Dev
   Module**, **USB CDC On Boot: Enabled**.
4. With the OG switched on, plug the Bottlenose's USB-C into the PC, pick its
   port and upload. If the upload cannot connect, hold BOOT, tap RESET,
   release BOOT, and upload again.

The sketch prints the same lines on its USB port, so the Arduino Serial
Monitor (115200) shows them too.

## Step 3: watch it

After a reset of either board, the LCD fills in within a few seconds:

| Row | Shows | Proves |
|---|---|---|
| C6 | `talking, fw 1.2` | lines are arriving over the header |
| WiFi | `up  <network>` | it joined your network |
| IP | the address your router gave it | DHCP works |
| RSSI | signal strength, coloured | the radio is receiving |
| Time | a clock ticking every second | NTP reached the internet |
| Web | your public IP | an HTTP GET to `api.ipify.org` worked |

LED bar: pixel 1 = C6, 2 = Wi-Fi, 3 = internet. Green is good, yellow is
pending, red is failed.

Buttons: **yellow** scans for networks, **green** fetches again, **blue** asks
the C6 to repeat its state. Red held 6 s still powers the board off.

On `fw console main` you can type the same commands (`SCAN`, `FETCH`,
`STATUS`, `HELP`) and they go to the C6. `!stats` shows the bridge's counters
and `!verbose` also shows the once-a-second `TIME` lines.

## Uploading rootclock games

Sketch 1.4 takes finished games from rootclock's main CPU (`RESULT BEGIN / DATA / END`
lines, with a length and CRC check), saves each one to the C6's flash and POSTs it to
root-metrics until the server has it. In `secrets.h`:

```c
#define RESULTS_URL   "https://<your-app>.vercel.app/api/results"  // or http://<lan-host>:8080/api/results
#define RESULTS_TOKEN "<root-metrics INGEST_TOKEN>"
```

- **Flash storage.** Saved games live in LittleFS on the C6's data partition. The default
  partition scheme of *ESP32C6 Dev Module* ("Default 4MB with spiffs") has one; the sketch
  formats it the first time. Uploading a new sketch does not erase it (unless *Erase All Flash
  Before Sketch Upload* is enabled), so games waiting there survive a reflash.
- **Size.** 1.3 uses 97% of that scheme's 1.2 MB app space (1.2 used 92%). If a later change
  stops fitting, pick *No OTA (2MB APP/2MB SPIFFS)* or *Huge APP (3MB No OTA/1MB SPIFFS)*:
  both keep a data partition. Changing the scheme moves that partition, so games still waiting
  on the C6 are lost; upload them first.
- **Checked so far.** Compiled with arduino-cli and the esp32 core 4.0.0 (*ESP32C6 Dev
  Module*, *USB CDC On Boot: Enabled*), no warnings from the sketch, also with *Huge APP*.
  Not yet run on a C6: the protocol side was exercised against `apps/rootclock/tools/sim/
  fake_c6.py`, which mirrors it.
- **HTTPS.** The sketch checks the server's certificate against `root_cas.h` (Google Trust
  Services for `*.vercel.app`, Let's Encrypt for most custom domains), after syncing the clock
  over NTP. `#define RESULTS_TLS_INSECURE 1` skips the check while testing another server.
- **Retries.** 5 s, 15 s, 60 s, then every 5 minutes while Wi-Fi is up. A wrong token (401/
  403) stops the retries until the C6 restarts; the games are kept, so reflashing with the
  right token sends them. A result the server calls invalid (other 4xx) is moved to `/bad/`.
- **Radio.** `WIFI OFF` (rootclock sends it at boot and during play) switches the radio off
  once nothing is waiting; `WIFI ON` (at GAME OVER) joins again. Without those commands, as
  with the bottlenose app, it behaves as before and stays joined.

New lines it sends: `QUEUE <n>` (games saved, not yet uploaded), `RESULT QUEUED|OK|ERR ...`
and `UPLOAD ...` progress. New commands: `QUEUE`, `WIFI ON`, `WIFI OFF`. The bottlenose app
shows them in its log like any other line.

## Changing what it fetches

`WEB_URL` and `TZ_INFO` are at the top of `bottlenose_c6.ino`. Keep the URL
plain `http://` and short. HTTPS needs `WiFiClientSecure`, and the display
has room for about 20 characters of the answer.

## If something is missing

- **Nothing at all from the C6:** header pin 4 (`V PINS IN`) must carry
  1.1-5.5 V or no breakout line works (see the BSP's `tools/io_walk.py`).
  The Bottlenose is expected to supply it. Check the Orca is fully seated.
- **`down: not found`:** the C6 cannot see the network. Press yellow (or type
  `SCAN`): the `SCAN yours:` line says whether it was seen at all, and a
  `close:` name shows a spelling difference such as a curly apostrophe
  (`\xe2\x80\x99`). Phone hotspots are often 5 GHz, which the C6 cannot see:
  on an iPhone turn on **Maximize Compatibility** and keep the Personal
  Hotspot screen open while it joins; on Android set the hotspot band to
  2.4 GHz.
- **`down: password?`:** the network refused the password.
- **`down: security`:** the network needs a login the sketch does not do,
  such as WPA2-Enterprise (eduroam and most campus networks). Use a WPA2
  home network or a phone hotspot instead.
- **`down: reason N`:** any other refusal; `N` is ESP-IDF's
  `wifi_err_reason_t` code.
- **`Web` red:** the C6 is on Wi-Fi but cannot reach the internet, or the
  URL is down. Press green to retry.
- **Garbled `[c6]` lines:** a baud mismatch. Both ends must be 115200.
