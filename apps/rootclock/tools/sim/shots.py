#!/usr/bin/env python3
"""rootclock simulator screenshots: every screen as a PNG, plus one sheet.

Runs the simulator (tools/sim, built with CMake) with SIM_FB set, walks
through a scripted game on its console with the same `press`/`hold` commands
hw_check.py uses, and saves the framebuffer after each step.

Usage:  python3 shots.py <path/to/rootclock_sim> <out_dir>
Writes <out_dir>/NN-<name>.png and <out_dir>/sheet.png. Standard library only.
"""
import os
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib

W, H = 320, 240
SETTLE_S = 0.45   # an injected tap is 100 ms, plus debounce, plus one redraw

# (name, steps). A step is a console command, ("sleep", seconds), or
# ("batt", "<mV> <usb 0|1> <charge 0|1|2>"), the simulated charger reading
# (the app reads it every 5 s).
SCENARIO = [
    ("ready", []),
    ("settings-players", ["press yellow"]),
    ("settings-players-3", ["press blue"]),
    ("settings-limit", ["press red", "press blue"]),
    ("settings-p2-faction", ["press red", "press red", "press red", "press red",
                             "press blue", "press blue", "press blue", "press blue"]),
    ("settings-p4-sits-out", ["press red", "press red", "press red", "press red"]),
    ("running-p1", ["press green", "press red", ("sleep", 2.2)]),
    ("running-p2", ["press green", ("sleep", 1.2)]),
    ("running-p1-again", ["press green", ("sleep", 1.1), "press green",
                          ("sleep", 1.0)]),
    ("paused-active", ["press red"]),
    ("paused-view-next", ["press green"]),
    ("paused-charging", [("batt", "4100 1 1"), ("sleep", 5.5)]),
    ("paused-on-usb", [("batt", "4200 1 2"), ("sleep", 5.5)]),
    ("paused-low-battery", [("batt", "3650 0 0"), ("sleep", 5.5)]),
    ("paused-hold", ["hold blue 1200", ("sleep", 0.6)]),
    ("over", [("sleep", 1.0), "hold blue 2500"]),
    ("over-view-next", ["press green"]),
]


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    # "P6\n320 240\n255\n" then raw RGB
    header_end = 0
    for _ in range(3):
        header_end = data.index(b"\n", header_end) + 1
    return data[header_end:]


def write_png(path, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def sheet(frames, cols=3, pad=8, bg=(64, 64, 64)):
    rows = (len(frames) + cols - 1) // cols
    sw, sh = cols * W + (cols + 1) * pad, rows * H + (rows + 1) * pad
    canvas = bytearray(bytes(bg) * sw * sh)
    for i, rgb in enumerate(frames):
        ox = pad + (i % cols) * (W + pad)
        oy = pad + (i // cols) * (H + pad)
        for y in range(H):
            dst = ((oy + y) * sw + ox) * 3
            canvas[dst:dst + W * 3] = rgb[y * W * 3:(y + 1) * W * 3]
    return sw, sh, bytes(canvas)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    sim, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="rootclock-sim-")
    fb = os.path.join(tmp, "fb.ppm")
    batt = os.path.join(tmp, "batt.txt")
    with open(batt, "w") as f:
        f.write("3950 0 0")
    env = dict(os.environ, SIM_FB=fb, SIM_BATT_FILE=batt,
               SIM_SEED=os.environ.get("SIM_SEED", "1"))
    p = subprocess.Popen([sim], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         env=env)
    replies = []
    threading.Thread(target=lambda: replies.extend(iter(p.stdout.readline, b"")),
                     daemon=True).start()
    frames = []
    try:
        time.sleep(0.6)
        for i, (name, steps) in enumerate(SCENARIO, 1):
            for step in steps:
                if isinstance(step, tuple) and step[0] == "batt":
                    with open(batt, "w") as f:
                        f.write(step[1])
                    continue
                if isinstance(step, tuple):
                    time.sleep(step[1])
                    continue
                p.stdin.write((step + "\n").encode())
                p.stdin.flush()
                # A hold under 2 s is captured while still held (the
                # progress bar); a longer one is waited out.
                hold_s = int(step.split()[2]) / 1000 if step.startswith("hold") else 0
                time.sleep(SETTLE_S + (hold_s if hold_s >= 2 else 0))
            time.sleep(0.15)
            rgb = read_ppm(fb)
            frames.append(rgb)
            write_png(os.path.join(out, f"{i:02d}-{name}.png"), W, H, rgb)
            print(f"{i:02d}-{name}.png")
        write_png(os.path.join(out, "sheet.png"), *sheet(frames))
        print("sheet.png")
    finally:
        p.stdin.close()
        p.wait(timeout=5)
    bad = [r for r in replies if r.startswith(b"ERR")]
    if bad:
        sys.exit("console errors: " + b" | ".join(bad).decode())


if __name__ == "__main__":
    main()
