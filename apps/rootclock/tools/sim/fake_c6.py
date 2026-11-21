#!/usr/bin/env python3
"""A stand-in for the Bottlenose ESP32-C6 (apps/bottlenose/c6/bottlenose_c6 1.3)
for the rootclock simulator: the same line protocol on stdin/stdout, a queue
directory standing in for the C6's flash, and real HTTP uploads.

    SIM_C6="python3 fake_c6.py --url http://127.0.0.1:8080/api/results --token dev" \
        build/rootclock-sim/rootclock_sim

Lines in:  STATUS | WIFI ON | WIFI OFF | QUEUE | RESULT BEGIN <id> <bytes> <crc32hex>
           | RESULT DATA <seq> <slice> | RESULT END <id>
Lines out: HELLO, WIFI <state>, QUEUE <n>, RESULT QUEUED|OK|ERR ... (see the sketch)

Without --url it plays a server that answers 201 (for screenshots).
--wifi never  keeps WiFi down ("not found"); --wifi-delay sets how long joining
takes. Standard library only.
"""
import argparse
import os
import select
import sys
import tempfile
import time
import urllib.error
import urllib.request
import zlib

BACKOFF = [5, 15, 60, 300]       # seconds between upload attempts, then every 5 min


def say(line):
    sys.stdout.write(line + "\n")
    sys.stdout.flush()


class C6:
    def __init__(self, a):
        self.a = a
        self.q = a.queue_dir or tempfile.mkdtemp(prefix="fake-c6-")
        os.makedirs(os.path.join(self.q, "bad"), exist_ok=True)
        self.wifi = "off"           # off | connecting | up | down: <why>
        self.want_on = True         # the real C6 joins at boot until told otherwise
        self.join_at = time.monotonic() + a.wifi_delay
        self.rx = None              # result being received
        self.next_try = 0.0
        self.tries = 0
        self.paused = False         # 401/403: wait for a restart
        self.set_wifi("connecting")

    # -- state reports --
    def queued(self):
        return sorted(f for f in os.listdir(self.q) if f.endswith(".json"))

    def set_wifi(self, s):
        if s != self.wifi:
            self.wifi = s
            say(f"WIFI {s}")

    def status(self):
        say("HELLO 1.3-sim")
        say(f"WIFI {self.wifi}")
        say(f"QUEUE {len(self.queued())}")

    # -- commands --
    def command(self, line):
        if line.startswith("RESULT "):
            return self.result_line(line)
        cmd = line.strip().upper()
        if cmd == "STATUS":
            self.status()
        elif cmd == "QUEUE":
            say(f"QUEUE {len(self.queued())}")
        elif cmd == "WIFI ON":
            self.want_on = True
            if self.wifi in ("off",) or self.wifi.startswith("down"):
                self.join_at = time.monotonic() + self.a.wifi_delay
                self.set_wifi("connecting")
            self.next_try = 0.0
        elif cmd == "WIFI OFF":
            self.want_on = False      # off once the queue is empty
        elif cmd:
            say(f"ERR unknown command: {cmd}")

    def result_line(self, line):
        parts = line.split(" ", 3)
        verb = parts[1] if len(parts) > 1 else ""
        if verb == "BEGIN" and len(parts) == 4:
            gid, rest = parts[2], parts[3].split()
            self.rx = {"id": gid, "bytes": int(rest[0]), "crc": int(rest[1], 16),
                       "data": [], "seq": 0}
        elif verb == "DATA" and self.rx and len(parts) == 4:
            if int(parts[2]) != self.rx["seq"]:
                self.rx["bad"] = True
            self.rx["seq"] += 1
            self.rx["data"].append(parts[3])
        elif verb == "END" and self.rx:
            rx, self.rx = self.rx, None
            body = "".join(rx["data"]).encode()
            if rx.get("bad") or len(body) != rx["bytes"]:
                return say(f"RESULT ERR {rx['id']} len")
            if zlib.crc32(body) & 0xFFFFFFFF != rx["crc"]:
                return say(f"RESULT ERR {rx['id']} crc")
            if self.a.no_url:
                return say(f"RESULT ERR {rx['id']} nourl")
            with open(os.path.join(self.q, rx["id"] + ".json"), "wb") as f:
                f.write(body)
            say(f"RESULT QUEUED {rx['id']} saved")
            say(f"QUEUE {len(self.queued())}")
            self.next_try = 0.0
            self.tries = 0

    # -- time-driven: WiFi and uploads --
    def tick(self):
        now = time.monotonic()
        if self.wifi == "connecting" and now >= self.join_at:
            self.set_wifi("down: not found" if self.a.wifi == "never" else "up")
        if self.wifi == "up" and not self.want_on and not self.queued():
            self.set_wifi("off")
        if self.wifi != "up" or self.paused or now < self.next_try:
            return
        q = self.queued()
        if not q:
            return
        name = q[0]
        gid = name[:-5]
        path = os.path.join(self.q, name)
        code = self.post(open(path, "rb").read())
        if code in (200, 201):
            os.remove(path)
            say(f"RESULT OK {gid} {code}")
            self.tries = 0
            self.next_try = now
        elif code in (401, 403):
            say(f"RESULT ERR {gid} {code}")
            self.paused = True       # the token is wrong; keep it, stop trying
        elif code is not None and 400 <= code < 500 and code not in (408, 429):
            os.replace(path, os.path.join(self.q, "bad", name))
            say(f"RESULT ERR {gid} {code}")
        else:
            say(f"RESULT QUEUED {gid} {'net' if code is None else 'http' + str(code)}")
            self.next_try = now + BACKOFF[min(self.tries, len(BACKOFF) - 1)]
            self.tries += 1
        say(f"QUEUE {len(self.queued())}")

    def post(self, body):
        if not self.a.url:
            return self.a.server_response
        req = urllib.request.Request(self.a.url, data=body, method="POST", headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {self.a.token}"})
        try:
            with urllib.request.urlopen(req, timeout=10) as r:
                return r.status
        except urllib.error.HTTPError as e:
            return e.code
        except Exception as e:  # no route, refused, TLS...
            print(f"fake_c6: POST failed: {e}", file=sys.stderr)
            return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--url", help="RESULTS_URL; omit to fake a server")
    ap.add_argument("--token", default="", help="RESULTS_TOKEN")
    ap.add_argument("--server-response", type=int, default=201,
                    help="status the fake server answers without --url")
    ap.add_argument("--queue-dir", help="stands in for the C6's flash (default: temp)")
    ap.add_argument("--wifi", choices=["ok", "never"], default="ok")
    ap.add_argument("--wifi-delay", type=float, default=1.5)
    ap.add_argument("--no-url", action="store_true", help="act as if secrets.h has no RESULTS_URL")
    c6 = C6(ap.parse_args())
    c6.status()
    buf = b""
    while True:
        r, _, _ = select.select([sys.stdin], [], [], 0.1)
        if r:
            chunk = os.read(sys.stdin.fileno(), 4096)
            if not chunk:
                return
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                c6.command(line.decode("ascii", "replace").rstrip("\r"))
        c6.tick()


if __name__ == "__main__":
    main()
