#!/usr/bin/env python3
"""Generate rootclock's faction icons as RGB565 C arrays.

Reads apps/rootclock/assets/factions/<key>.png (RGBA, real transparency) for
the 13 factions in rc_faction_id_t order and writes
apps/rootclock/display/assets/faction_icons.c, which is checked in so the
firmware build never needs Pillow.

Per icon:
  1. open as RGBA;
  2. drop stray specks: 8-connected regions of alpha > 16 smaller than 5% of
     the largest region get alpha 0 (some sources carry background-removal
     residue that would otherwise inflate the crop);
  3. crop to the bounding box of alpha > 16;
  4. fit inside 96x96 with a 4 px margin (max 88x88) keeping the aspect
     ratio, LANCZOS, and centre it;
  5. alpha-composite onto a solid #EDE3C8 tile (UI_TILE in ui_palette.h);
  6. pack each pixel like the BSP's st7789_rgb565().

The output is byte-for-byte deterministic for a given Pillow version: no
timestamps, no absolute paths.

    py apps/rootclock/tools/gen_faction_icons.py
    py apps/rootclock/tools/gen_faction_icons.py --sheet icons.png

--sheet also writes a contact sheet of all 13 tiles at 2x, decoded back from
the RGB565 values, so it shows what the LCD will get.
"""

from pathlib import Path
import argparse
import math
import re
import sys

from PIL import Image, ImageDraw, ImageFont, ImageOps


ROOT = Path(__file__).resolve().parents[3]
APP = ROOT / "apps" / "rootclock"
DEFAULT_SRC = APP / "assets" / "factions"
DEFAULT_OUT = APP / "display" / "assets" / "faction_icons.c"
RC_TYPES_H = APP / "core" / "rc_types.h"
UI_PALETTE_H = APP / "display" / "ui_palette.h"
FACTION_ICONS_H = APP / "display" / "assets" / "faction_icons.h"

# rc_faction_id_t order -- load-bearing, checked against rc_types.h below.
KEYS = (
    "marquise", "eyrie", "woodland", "vagabond", "riverfolk", "cult", "duchy",
    "corvid", "hundreds", "keepers", "twilight", "knaves", "lilypad",
)

ICON_W = 96             # RC_ICON_W
ICON_H = 96             # RC_ICON_H
MARGIN = 4
TILE_RGB = (0xED, 0xE3, 0xC8)   # UI_TILE
ALPHA_THRESHOLD = 16
SPECK_FRACTION = 0.05
PER_LINE = 12

SHEET_SCALE = 2
SHEET_COLS = 5
SHEET_PAD = 12
SHEET_LABEL_H = 24


def fail(msg):
    sys.exit(f"gen_faction_icons: error: {msg}")


def rel(path):
    """Repo-relative POSIX path when possible, so the output never embeds
    where the repository happens to be checked out."""
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def check_contract():
    """Fail loudly if the headers this generator mirrors have drifted."""
    text = RC_TYPES_H.read_text(encoding="utf-8")
    enum = re.search(r"typedef enum \{(.*?)\}\s*rc_faction_id_t;", text, re.S)
    if not enum:
        fail(f"rc_faction_id_t not found in {rel(RC_TYPES_H)}")
    order = tuple(
        m.lower() for m in re.findall(r"RC_FACTION_(\w+)", enum.group(1))
        if m != "COUNT"
    )
    if order != KEYS:
        fail(f"KEYS {KEYS} do not match rc_faction_id_t {order}")

    text = UI_PALETTE_H.read_text(encoding="utf-8")
    tile = re.search(r"#define\s+UI_TILE\s+UI_RGB\((\d+),\s*(\d+),\s*(\d+)\)", text)
    if not tile or tuple(int(v) for v in tile.groups()) != TILE_RGB:
        fail(f"UI_TILE in {rel(UI_PALETTE_H)} is not UI_RGB{TILE_RGB}")

    text = FACTION_ICONS_H.read_text(encoding="utf-8")
    for name, want in (("RC_ICON_W", ICON_W), ("RC_ICON_H", ICON_H)):
        m = re.search(rf"#define\s+{name}\s+(\d+)u?\b", text)
        if not m or int(m.group(1)) != want:
            fail(f"{name} in {rel(FACTION_ICONS_H)} is not {want}")


def rgb565(r, g, b):
    """Exactly st7789_rgb565() in bsp/display_cpu/lcd/st7789.c."""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def rgb888(v):
    """Expand RGB565 back to 8 bits per channel (bit replication)."""
    r, g, b = v >> 11, (v >> 5) & 0x3F, v & 0x1F
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def drop_specks(img):
    """Zero the alpha of 8-connected alpha > ALPHA_THRESHOLD regions smaller
    than SPECK_FRACTION of the largest. Returns (image, regions, pixels)
    dropped; the image is returned untouched when nothing is dropped."""
    w, h = img.size
    alpha = img.getchannel("A")
    mask = alpha.point(lambda v: 1 if v > ALPHA_THRESHOLD else 0).tobytes()
    labels = [0] * (w * h)
    sizes = [0]                         # sizes[label]; label 0 = background
    for start in range(w * h):
        if not mask[start] or labels[start]:
            continue
        label = len(sizes)
        labels[start] = label
        stack = [start]
        n = 0
        while stack:
            i = stack.pop()
            n += 1
            y, x = divmod(i, w)
            for ny in (y - 1, y, y + 1):
                if ny < 0 or ny >= h:
                    continue
                row = ny * w
                for nx in (x - 1, x, x + 1):
                    if nx < 0 or nx >= w:
                        continue
                    j = row + nx
                    if mask[j] and not labels[j]:
                        labels[j] = label
                        stack.append(j)
        sizes.append(n)
    if len(sizes) <= 2:
        return img, 0, 0
    keep_min = SPECK_FRACTION * max(sizes)
    dropped = {lab for lab in range(1, len(sizes)) if sizes[lab] < keep_min}
    if not dropped:
        return img, 0, 0
    a = bytearray(alpha.tobytes())
    for i, lab in enumerate(labels):
        if lab in dropped:
            a[i] = 0
    out = img.copy()
    out.putalpha(Image.frombytes("L", (w, h), bytes(a)))
    return out, len(dropped), sum(sizes[lab] for lab in dropped)


def convert_icon(path):
    with Image.open(path) as src:
        img = src.convert("RGBA")
    img, specks, speck_px = drop_specks(img)
    bbox = img.getchannel("A").point(
        lambda v: 255 if v > ALPHA_THRESHOLD else 0).getbbox()
    if bbox is None:
        fail(f"{rel(path)}: no pixel has alpha > {ALPHA_THRESHOLD}")
    cropped = img.crop(bbox)
    fitted = ImageOps.contain(
        cropped, (ICON_W - 2 * MARGIN, ICON_H - 2 * MARGIN),
        Image.Resampling.LANCZOS)
    pos = ((ICON_W - fitted.width) // 2, (ICON_H - fitted.height) // 2)
    tile = Image.new("RGBA", (ICON_W, ICON_H), TILE_RGB + (255,))
    tile.alpha_composite(fitted, pos)
    rgb = tile.convert("RGB").tobytes()
    pixels = [rgb565(rgb[i], rgb[i + 1], rgb[i + 2])
              for i in range(0, len(rgb), 3)]
    info = (f"source {img.width}x{img.height}, crop {cropped.width}x"
            f"{cropped.height} at ({bbox[0]},{bbox[1]}), scaled to "
            f"{fitted.width}x{fitted.height} at ({pos[0]},{pos[1]})")
    if specks:
        info += f", {specks} specks ({speck_px} px) dropped"
    return pixels, info


def emit_c(out_path, src_dir, icons):
    lines = [
        "/* rootclock -- faction icons: RC_ICON_W x RC_ICON_H RGB565, in",
        " * rc_faction_id_t order.",
        " *",
        " * GENERATED by apps/rootclock/tools/gen_faction_icons.py -- do not edit.",
        " * Regenerate with:  py apps/rootclock/tools/gen_faction_icons.py",
        " *",
        f" * Source: {rel(src_dir)}/<key>.png. Each icon has stray specks",
        f" * (8-connected alpha > {ALPHA_THRESHOLD} regions under "
        f"{round(SPECK_FRACTION * 100)}% of the largest) removed, is",
        f" * cropped to the bounding box of alpha > {ALPHA_THRESHOLD}, fitted inside "
        f"{ICON_W - 2 * MARGIN}x{ICON_H - 2 * MARGIN}",
        f" * ({MARGIN} px margin) keeping its aspect ratio (LANCZOS), centred, and",
        " * alpha-composited onto the tile colour #%02X%02X%02X (UI_TILE), then packed"
        % TILE_RGB,
        " * like st7789_rgb565(): ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3). */",
        "",
        "#include <stddef.h>",
        "",
        '#include "faction_icons.h"',
        '#include "rc_types.h"',
        "",
    ]
    for key, (pixels, info) in zip(KEYS, icons):
        lines.append(f"/* {key}: {info} */")
        lines.append(f"static const uint16_t s_px_{key}[RC_ICON_W * RC_ICON_H] = {{")
        for i in range(0, len(pixels), PER_LINE):
            lines.append("    " + ", ".join(
                f"0x{v:04X}" for v in pixels[i:i + PER_LINE]) + ",")
        lines.append("};")
        lines.append("")
    lines.append("static const rc_icon_t s_icons[] = {")
    for key in KEYS:
        lines.append(f"    {{ RC_ICON_W, RC_ICON_H, s_px_{key} }},")
    lines += [
        "};",
        "_Static_assert(sizeof s_icons / sizeof s_icons[0] == (size_t)RC_FACTION_COUNT,",
        '               "one icon per rc_faction_id_t, in enum order");',
        "",
        "static const rc_icon_t s_none = { RC_ICON_W, RC_ICON_H, NULL };",
        "",
        "const rc_icon_t *rc_icon_get(uint8_t faction_id) {",
        "    if (faction_id < RC_FACTION_COUNT) {",
        "        return &s_icons[faction_id];",
        "    }",
        "    return &s_none;",
        "}",
    ]
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="ascii", newline="\n") as out:
        out.write("\n".join(lines) + "\n")


def load_font(size):
    try:
        return ImageFont.load_default(size=size)
    except (TypeError, OSError, ImportError):
        return ImageFont.load_default()


def write_sheet(sheet_path, icons):
    cell_w = ICON_W * SHEET_SCALE
    cell_h = ICON_H * SHEET_SCALE + SHEET_LABEL_H
    rows = math.ceil(len(KEYS) / SHEET_COLS)
    sheet = Image.new(
        "RGB",
        (SHEET_PAD + SHEET_COLS * (cell_w + SHEET_PAD),
         SHEET_PAD + rows * (cell_h + SHEET_PAD)),
        (0, 0, 0))                      # UI_BG: what surrounds a tile on the LCD
    draw = ImageDraw.Draw(sheet)
    font = load_font(16)
    for n, (key, (pixels, _)) in enumerate(zip(KEYS, icons)):
        tile = Image.new("RGB", (ICON_W, ICON_H))
        tile.putdata([rgb888(v) for v in pixels])
        tile = tile.resize((cell_w, ICON_H * SHEET_SCALE), Image.Resampling.NEAREST)
        x = SHEET_PAD + (n % SHEET_COLS) * (cell_w + SHEET_PAD)
        y = SHEET_PAD + (n // SHEET_COLS) * (cell_h + SHEET_PAD)
        sheet.paste(tile, (x, y))
        draw.text((x + cell_w // 2, y + ICON_H * SHEET_SCALE + SHEET_LABEL_H // 2),
                  f"{n} {key}", fill=(255, 255, 255), font=font, anchor="mm")
    sheet_path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(sheet_path)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--src", type=Path, default=DEFAULT_SRC,
                        help=f"directory of <key>.png (default {rel(DEFAULT_SRC)})")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT,
                        help=f"C file to write (default {rel(DEFAULT_OUT)})")
    parser.add_argument("--sheet", type=Path,
                        help="also write a contact sheet PNG decoded from the RGB565 data")
    args = parser.parse_args()

    check_contract()
    missing = [k for k in KEYS if not (args.src / f"{k}.png").is_file()]
    if missing:
        fail(f"missing source PNG(s) in {rel(args.src)}: "
             + ", ".join(f"{k}.png" for k in missing))

    icons = []
    for key in KEYS:
        pixels, info = convert_icon(args.src / f"{key}.png")
        print(f"{key:10s} {info}")
        icons.append((pixels, info))

    emit_c(args.out, args.src, icons)
    print(f"wrote {rel(args.out)} ({len(KEYS)} x {ICON_W}x{ICON_H} RGB565, "
          f"{len(KEYS) * ICON_W * ICON_H * 2} bytes of pixels)")
    if args.sheet:
        write_sheet(args.sheet, icons)
        print(f"wrote {args.sheet}")


if __name__ == "__main__":
    main()
