#!/usr/bin/env python3
"""Upload rootclock results to root-metrics from a computer -- no WiFi module.

The fallback path in documents/04: the display CPU prints every finished game
as a `RESULT {json}` line on its USB console, and this script posts it to
root-metrics' POST /api/results with the device token.

  py apps/rootclock/tools/upload_results.py --url https://<app>.vercel.app/api/results --token T
      asks the board for its last result (`result`) and uploads it
  ... --watch      keeps the console open and uploads each game as it ends
  ... --file game.json [more.json ...]   uploads saved JSON files instead

SAFETY: the port is opened at 115200 with the baud set before open. Never
1200: that reboots the CPU into BOOTSEL. --port also accepts the simulator's
pty (tools/sim). Without --port it finds the display CPU (USB 093C:2055).

Uploading the same game twice is harmless: the server answers 200 duplicate.
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request


def post(url, token, body):
    req = urllib.request.Request(url, data=body.encode(), method="POST", headers={
        "Content-Type": "application/json", "Authorization": f"Bearer {token}"})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")


def upload(args, body):
    try:
        gid = json.loads(body).get("game_id", "?")
    except ValueError:
        gid = "?"
    code, text = post(args.url, args.token, body)
    ok = code in (200, 201)
    print(f"{'OK  ' if ok else 'FAIL'} {gid}: HTTP {code} {text.strip()[:200]}")
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--url", required=True, help="root-metrics .../api/results")
    ap.add_argument("--token", default=os.environ.get("INGEST_TOKEN", ""),
                    help="the server's INGEST_TOKEN (default: $INGEST_TOKEN)")
    ap.add_argument("--port", help="display CPU port (default: find 093C:2055)")
    ap.add_argument("--watch", action="store_true", help="upload every game as it ends")
    ap.add_argument("--file", nargs="+", help="upload these JSON files instead")
    args = ap.parse_args()
    if not args.token:
        sys.exit("--token (or INGEST_TOKEN) is required")

    if args.file:
        ok = all([upload(args, open(p, encoding="utf-8").read().strip()) for p in args.file])
        return 0 if ok else 1

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from hw_check import find_port   # same port finder and safe open as the AT run

    b = find_port(args.port, 30.0)
    print(f"display CPU on {b.ser.port}")
    try:
        if not args.watch:
            reply = b.cmd("result", prefixes=("RESULT ", "ERR"))
            if reply.startswith("ERR"):
                print(f"no result on the board: {reply}")
                return 1
            return 0 if upload(args, reply[len("RESULT "):]) else 1
        print("watching; Ctrl+C to stop")
        b.results.clear()
        while True:
            b.drain(0.5)
            while b.results:
                upload(args, b.results.pop(0))
            time.sleep(0.2)
    except KeyboardInterrupt:
        return 0
    finally:
        b.close()


if __name__ == "__main__":
    sys.exit(main())
