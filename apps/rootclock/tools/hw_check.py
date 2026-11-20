#!/usr/bin/env python3
"""rootclock hardware acceptance check -- runs documents/02 AT-01..AT-14 on a board.

Drives the display CPU's test console (see apps/rootclock/display/main.c) over
USB CDC: injected button presses run through the real debounce and power path,
and `state` reports the game model. Checks states, seats, settings, timing
(+-TOL_MS against the host clock), the alarm event count, the
global == sum(totals) invariant on every snapshot, and AT-11's result JSON
against payload contract v1 (documents/04 section 2).

What it cannot check needs eyes and ears: layout, colours, icons, red digits,
the beep itself, the LED flicker (AT-14) and power-off (AT-15).

SAFETY
  * The port is opened at 115200 with the baud rate set BEFORE open. Never
    1200: opening a CPU's port at 1200 baud reboots it into BOOTSEL.
  * Red is never held for 1 s or more (the firmware also refuses): 6 s powers
    the board off and only a human can switch it back on.

Usage:  py apps/rootclock/tools/hw_check.py [--port COMx] [--wait 60]
Exit status 0 when every check passes.
"""
import argparse
import json
import re
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover
    sys.exit("pyserial is required: py -m pip install pyserial")

ICS_VID = 0x093C
DISPLAY_PID = 0x2055          # display app AND display bootloader
BAUD = 115200                 # NEVER 1200
TOL_MS = 300
TAP_SETTLE_S = 0.25           # 100 ms injected tap + 5 ms debounce + margin
RED_HOLD_LIMIT_MS = 1000

NICKS = ["Jacob", "Alex", "Ahmed", "Lorenzo"]
FACTION_KEYS = ["marquise", "eyrie", "woodland", "vagabond", "riverfolk", "cult",
                "duchy", "corvid", "hundreds", "keepers", "twilight", "knaves",
                "lilypad"]


class Board:
    def __init__(self, port):
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = BAUD          # set before open(): never a 1200 transient
        self.ser.timeout = 0.05
        self.ser.write_timeout = 1.0
        self.ser.dtr = True               # pico stdio_usb only talks with DTR set
        self.ser.open()
        self.buf = b""
        self.results = []                 # every "RESULT {...}" line seen

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    def _read_line(self, deadline):
        while time.monotonic() < deadline:
            nl = self.buf.find(b"\n")
            if nl >= 0:
                raw, self.buf = self.buf[:nl], self.buf[nl + 1:]
                line = raw.decode("utf-8", "replace").strip()
                if line.startswith("RESULT "):
                    self.results.append(line[len("RESULT "):])
                return line
            chunk = self.ser.read(256)
            if chunk:
                self.buf += chunk
        return None

    def drain(self, seconds=0.2):
        deadline = time.monotonic() + seconds
        while self._read_line(deadline) is not None:
            pass

    def cmd(self, text, prefixes=("OK", "ERR"), timeout=2.0):
        self.ser.write((text + "\n").encode("ascii"))
        deadline = time.monotonic() + timeout
        while True:
            line = self._read_line(deadline)
            if line is None:
                raise TimeoutError(f"no reply to {text!r}")
            if line.startswith(prefixes):
                return line

    def state(self):
        line = self.cmd("state", prefixes=("STATE ",))
        return json.loads(line[len("STATE "):])

    def press(self, btn):
        reply = self.cmd(f"press {btn}")
        if not reply.startswith("OK"):
            raise RuntimeError(f"press {btn}: {reply}")
        time.sleep(TAP_SETTLE_S)

    def hold(self, btn, ms):
        if btn == "red" and ms >= RED_HOLD_LIMIT_MS:
            raise ValueError("refusing to hold red for 1 s or more")
        reply = self.cmd(f"hold {btn} {ms}")
        if not reply.startswith("OK"):
            raise RuntimeError(f"hold {btn}: {reply}")
        time.sleep(ms / 1000.0 + TAP_SETTLE_S)


class Checker:
    def __init__(self):
        self.fails = []
        self.passes = 0
        self.section = ""

    def at(self, name):
        self.section = name
        print(f"\n{name}")

    def ok(self, cond, msg):
        if cond:
            self.passes += 1
            print(f"  PASS {msg}")
        else:
            self.fails.append(f"{self.section}: {msg}")
            print(f"  FAIL {msg}")
        return cond

    def snapshot(self, s):
        """Checks that hold in every state: global == sum(totals)."""
        self.ok(s["global_ms"] == sum(s["totals"]),
                f"global_ms {s['global_ms']} == sum(totals) {sum(s['totals'])}")
        return s


def approx(actual_ms, expected_ms, tol=TOL_MS):
    return abs(actual_ms - expected_ms) <= tol


def find_port(explicit, wait_s):
    """The display CPU's port that answers `state` as rootclock."""
    deadline = time.monotonic() + wait_s
    last_err = "no 093C:2055 port found"
    while time.monotonic() < deadline:
        ports = [explicit] if explicit else [
            p.device for p in list_ports.comports()
            if p.vid == ICS_VID and p.pid == DISPLAY_PID]
        for dev in ports:
            b = None
            try:
                b = Board(dev)
                b.drain(0.3)
                s = b.state()
                if s.get("app") == "rootclock":
                    return b
                last_err = f"{dev} answered but is not rootclock"
            except Exception as e:  # not ready yet, bootloader, busy port...
                last_err = f"{dev}: {e}"
            if b:
                b.close()
        time.sleep(1.0)
    raise SystemExit(f"display CPU not reachable: {last_err}")


def check_result(c, raw, expect):
    """Contract v1 (documents/04 section 2) plus this script's scenario."""
    try:
        r = json.loads(raw)
    except ValueError as e:
        c.ok(False, f"RESULT is valid JSON ({e})")
        return None
    c.ok(raw == json.dumps(r, separators=(",", ":")), "RESULT is compact one-line JSON")
    c.ok(list(r.keys()) == ["schema", "schema_version", "game_id", "device", "ended_at",
                            "turn_limit_ms", "global_ms", "players"], "top-level key order")
    c.ok(r.get("schema") == "rootclock.game_result", "schema")
    c.ok(r.get("schema_version") == 1, "schema_version == 1")
    c.ok(bool(re.fullmatch(r"[0-9a-f]{16}", str(r.get("game_id")))), "game_id is 16 hex")
    c.ok(r.get("device") == {"app": "rootclock", "app_version": "001"}, "device")
    c.ok(r.get("ended_at") is None, "ended_at null")
    c.ok(r.get("turn_limit_ms") == expect["limit_ms"], f"turn_limit_ms == {expect['limit_ms']}")
    players = r.get("players", [])
    c.ok([p.get("seat") for p in players] == expect["seats"], f"seats {expect['seats']}")
    c.ok([p.get("nickname") for p in players] == expect["nicks"], f"nicknames {expect['nicks']}")
    c.ok([p.get("faction") for p in players] == expect["factions"],
         f"factions {expect['factions']}")
    c.ok([len(p.get("turns_ms", [])) for p in players] == expect["turn_counts"],
         f"turn counts {expect['turn_counts']}")
    for p in players:
        c.ok(list(p.keys()) == ["seat", "nickname", "faction", "total_ms", "turns_ms",
                                "turns_truncated"], f"seat {p.get('seat')} key order")
        c.ok(p.get("total_ms") == sum(p.get("turns_ms", [])),
             f"seat {p.get('seat')} total_ms == sum(turns_ms)")
        c.ok(p.get("turns_truncated") is False, f"seat {p.get('seat')} not truncated")
    c.ok(r.get("global_ms") == sum(p.get("total_ms", 0) for p in players),
         "global_ms == sum(total_ms)")
    return r


def run(b):
    c = Checker()

    c.at("setup: reset to a fresh READY game with default settings")
    c.ok(b.cmd("reset").startswith("OK"), "reset accepted")
    b.results.clear()

    c.at("AT-01 boot state")
    s = c.snapshot(b.state())
    c.ok(s["state"] == "READY" and s["screen"] == "S1", "READY on S1")
    c.ok(s["shown"] == 0, "shows P1")
    c.ok(s["nick"] == [0, 1, 2, 3], "nicknames Jacob, Alex, Ahmed, Lorenzo")
    c.ok(s["fac"] == [0, 1, 2, 3], "factions Marquise, Eyrie, Woodland, Vagabond")
    c.ok(s["global_ms"] == 0 and s["turn_ms"] == 0 and s["totals"] == [0, 0, 0, 0],
         "all clocks zero")
    c.ok(s["limit_ms"] == 0, "limit off")

    c.at("AT-02 settings: open, move, change P2 faction to Lizard Cult")
    b.press("yellow")
    s = b.state()
    c.ok(s["state"] == "SETTINGS" and s["screen"] == "S2" and s["row"] == 0,
         "yellow opens S2 at row 0")
    for _ in range(4):
        b.press("red")
    c.ok(b.state()["row"] == 4, "red x4 -> row 4 (P2 faction)")
    for _ in range(4):
        b.press("blue")
    s = b.state()
    c.ok(s["fac"][1] == FACTION_KEYS.index("cult"), "blue x4: Eyrie -> Lizard Cult")

    c.at("AT-03 nickname swap")
    for _ in range(3):
        b.press("gray")
    c.ok(b.state()["row"] == 1, "gray x3 -> row 1 (P1 nickname)")
    b.press("blue")
    s = b.state()
    c.ok(s["nick"][0] == 1 and s["nick"][1] == 0, "P1 -> Alex, P2 -> Jacob (swapped)")
    c.ok(sorted(s["nick"]) == [0, 1, 2, 3], "nicknames still a permutation")

    c.at("AT-04 limit 0:30, back to READY")
    b.press("gray")
    c.ok(b.state()["row"] == 0, "gray -> row 0 (LIMIT)")
    b.press("blue")
    c.ok(b.state()["limit_ms"] == 30000, "blue: Off -> 0:30")
    b.press("green")
    s = b.state()
    c.ok(s["state"] == "READY" and s["screen"] == "S1", "green returns to READY on S1")

    c.at("AT-05 start: P1's turn counts, global counts with it")
    b.press("red")
    t_p1 = time.monotonic() - TAP_SETTLE_S
    s = b.state()
    c.ok(s["state"] == "RUNNING" and s["active"] == 0, "RUNNING, P1 active")
    time.sleep(2.0)
    s = c.snapshot(b.state())
    expect = (time.monotonic() - t_p1) * 1000
    c.ok(approx(s["turn_ms"], expect), f"P1 turn {s['turn_ms']} ms ~ {expect:.0f} ms")
    c.ok(s["global_ms"] == s["turn_ms"], "global == P1 turn (only P1 has played)")

    c.at("AT-06 P1's own button is a no-op")
    before = b.state()["turn_ms"]
    b.press("gray")
    s = b.state()
    c.ok(s["active"] == 0 and s["turn_ms"] >= before and s["turns"][0] == 1,
         f"still P1's first turn, not reset ({before} -> {s['turn_ms']} ms)")

    c.at("AT-07 switch to P2: new turn from zero, P1 total kept")
    b.press("yellow")
    t_p2 = time.monotonic() - TAP_SETTLE_S
    s = c.snapshot(b.state())
    c.ok(s["active"] == 1 and s["turns"][1] == 1, "P2 active, first turn")
    c.ok(s["turn_ms"] < 600, f"P2 turn restarted ({s['turn_ms']} ms)")
    c.ok(s["logged"][0] == 1, "P1's turn logged")
    p1_first = s["totals"][0]
    c.ok(p1_first > 2500, f"P1 total kept ({p1_first} ms)")

    c.at("AT-08 limit: one beep and red at 0:30, keeps counting")
    time.sleep(max(0.0, 28.5 - (time.monotonic() - t_p2)))
    s = b.state()
    c.ok(s["over"] is False and s["beeps"] == 0, f"before the limit: white, no beep ({s['turn_ms']} ms)")
    time.sleep(max(0.0, 31.0 - (time.monotonic() - t_p2)))
    s = c.snapshot(b.state())
    c.ok(s["over"] is True and s["beeps"] == 1, f"past the limit: red, one beep ({s['turn_ms']} ms)")
    time.sleep(2.0)
    s2 = b.state()
    c.ok(s2["turn_ms"] > s["turn_ms"] + 1500, "still counting past the limit")
    c.ok(s2["beeps"] == 1, "beeped only once this turn")

    c.at("AT-09 back to P1: white again, total resumes from the kept value")
    b.press("gray")
    s = c.snapshot(b.state())
    c.ok(s["active"] == 0 and s["over"] is False and s["turn_ms"] < 600,
         "P1 active, new turn from zero, white")
    c.ok(s["turns"][0] == 2 and s["logged"][1] == 1, "P1 second turn; P2's turn logged")
    c.ok(p1_first <= s["totals"][0] <= p1_first + 600, f"P1 total {s['totals'][0]} ms")

    c.at("AT-10 pause freezes all three timers; resume continues the same turn")
    b.press("red")
    s1 = b.state()
    c.ok(s1["state"] == "PAUSED", "red pauses")
    time.sleep(3.0)
    s2 = c.snapshot(b.state())
    c.ok(s2["turn_ms"] == s1["turn_ms"] and s2["totals"] == s1["totals"]
         and s2["global_ms"] == s1["global_ms"], "turn, totals and global frozen for 3 s")
    b.press("red")
    s3 = b.state()
    c.ok(s3["state"] == "RUNNING" and s3["turns"][0] == 2, "red resumes the same turn")
    c.ok(s1["turn_ms"] <= s3["turn_ms"] < s1["turn_ms"] + 600,
         f"turn continues from the frozen value ({s1['turn_ms']} -> {s3['turn_ms']} ms)")

    c.at("AT-11 end game: P3 plays, pause, hold blue 2 s -> OVER + result JSON")
    b.press("green")
    c.ok(b.state()["active"] == 2, "P3 active")
    time.sleep(2.0)
    b.press("red")
    b.hold("blue", 2500)
    b.drain(0.5)
    s = c.snapshot(b.state())
    c.ok(s["state"] == "OVER" and s["games_over"] == 1, "OVER, one game-over event")
    c.ok(s["viewed"] == 2, "shows the last active player (P3)")
    if not b.results:
        reply = b.cmd("result", prefixes=("RESULT", "ERR"))
        if reply.startswith("ERR"):
            c.ok(False, f"result available ({reply})")
    c.ok(len(b.results) >= 1, "RESULT line printed")
    if b.results:
        r = check_result(c, b.results[-1], {
            "limit_ms": 30000,
            "seats": [1, 2, 3],
            "nicks": ["Alex", "Jacob", "Ahmed"],
            "factions": ["marquise", "cult", "woodland"],
            "turn_counts": [2, 1, 1],
        })
        if r:
            c.ok(r["global_ms"] == s["global_ms"], "JSON global_ms == device global_ms")
            p2 = r["players"][1]["turns_ms"][0] if len(r["players"]) > 1 else 0
            c.ok(p2 > 30000, f"P2's turn ({p2} ms) is past the 0:30 limit")

    c.at("AT-12 view each player's final card; a blue tap does not start a new game")
    for btn, seat in (("gray", 0), ("yellow", 1), ("green", 2), ("blue", 3)):
        b.press(btn)
        s = b.state()
        c.ok(s["state"] == "OVER" and s["viewed"] == seat, f"{btn} -> P{seat + 1} card")
    s = c.snapshot(b.state())
    c.ok(s["totals"][3] == 0, "P4 never played: total 0")

    c.at("AT-13 hold blue 2 s -> new game, settings kept")
    b.hold("blue", 2500)
    s = c.snapshot(b.state())
    c.ok(s["state"] == "READY", "READY")
    c.ok(s["totals"] == [0, 0, 0, 0] and s["turns"] == [0, 0, 0, 0], "clocks and turns reset")
    c.ok(s["limit_ms"] == 30000 and s["nick"][0] == 1 and s["fac"][1] == 5,
         "settings kept (0:30, P1 Alex, P2 Lizard Cult)")

    c.at("AT-14 quick red taps: start then pause; the board stays on")
    b.press("red")
    c.ok(b.state()["state"] == "RUNNING", "red starts")
    b.press("red")
    c.ok(b.state()["state"] == "PAUSED", "red tap pauses; board still answering")

    c.at("teardown: reset to defaults")
    c.ok(b.cmd("reset").startswith("OK"), "reset accepted")
    return c


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--port", help="display CPU COM port (default: find 093C:2055)")
    ap.add_argument("--wait", type=float, default=60.0,
                    help="seconds to wait for the display CPU to answer")
    args = ap.parse_args()

    b = find_port(args.port, args.wait)
    print(f"display CPU on {b.ser.port}")
    try:
        c = run(b)
    finally:
        b.close()

    print(f"\n{c.passes} passed, {len(c.fails)} failed")
    for f in c.fails:
        print(f"  FAIL {f}")
    print("\nNot checkable from here (developer, by eye/ear): layout, colours, icons,"
          " red digits, the beep (AT-08), LED flicker (AT-14), power-off (AT-15).")
    return 1 if c.fails else 0


if __name__ == "__main__":
    sys.exit(main())
