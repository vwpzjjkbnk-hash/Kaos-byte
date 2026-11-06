#!/usr/bin/env python3
"""Makes the phone remote's small pictures (The Static Rift\\Phone) from the menu's pictures.

The TV menu uses QOI pictures (Images\\, UI\\): quick for the console to draw, but about 110 KB a
card, too heavy to send a phone a screenful at a time over the Xbox's Wi-Fi. This writes, next to
them:

  Phone\\<same folders as Images>\\<name>.jpg   the card's top, square, 160 x 160 (about 6 KB)
  Phone\\elements\\<element>.png                 element icons, 64 x 64
  Phone\\games\\<n>.png                          game logos
  Phone\\background.jpg                          the page background

The page uses these when they're there and falls back to the QOI pictures when they aren't (a
figure you added yourself, say). Run it again after adding pictures.

Usage: mkphone.py "<The Static Rift folder>" [--size 160] [--quality 74]
"""

import argparse
import os
import struct
import sys

from PIL import Image


def read_qoi(path):
    data = open(path, "rb").read()
    if data[:4] != b"qoif":
        raise ValueError("not a QOI picture")
    w, h = struct.unpack(">II", data[4:12])
    px = bytearray(w * h * 4)
    index = [(0, 0, 0, 0)] * 64
    r = g = b = 0
    a = 255
    p, run = 14, 0
    end = len(data) - 8
    for o in range(0, len(px), 4):
        if run:
            run -= 1
        elif p < end:
            b1 = data[p]
            p += 1
            if b1 == 0xFE:
                r, g, b = data[p], data[p + 1], data[p + 2]
                p += 3
            elif b1 == 0xFF:
                r, g, b, a = data[p], data[p + 1], data[p + 2], data[p + 3]
                p += 4
            elif b1 >> 6 == 0:
                r, g, b, a = index[b1]
            elif b1 >> 6 == 1:
                r = (r + ((b1 >> 4) & 3) - 2) & 255
                g = (g + ((b1 >> 2) & 3) - 2) & 255
                b = (b + (b1 & 3) - 2) & 255
            elif b1 >> 6 == 2:
                b2 = data[p]
                p += 1
                dg = (b1 & 63) - 32
                r = (r + dg - 8 + ((b2 >> 4) & 15)) & 255
                g = (g + dg) & 255
                b = (b + dg - 8 + (b2 & 15)) & 255
            else:
                run = b1 & 63
            index[(r * 3 + g * 5 + b * 7 + a * 11) % 64] = (r, g, b, a)
        px[o:o + 4] = bytes((r, g, b, a))
    return Image.frombytes("RGBA", (w, h), bytes(px))


def flatten(img, colour=(13, 26, 58)):
    """Transparent edges onto the page's card colour (JPEG has no transparency)."""
    if img.mode != "RGBA":
        return img.convert("RGB")
    bg = Image.new("RGB", img.size, colour)
    bg.paste(img, mask=img.split()[3])
    return bg


def card_thumb(img, size):
    # The same part of the card the page shows: a square from near the top (CSS "center 12%").
    w, h = img.size
    side = min(w, h)
    y0 = int(round((h - side) * 0.12))
    x0 = (w - side) // 2
    return flatten(img.crop((x0, y0, x0 + side, y0 + side))).resize((size, size), Image.LANCZOS)


def newer(src, dst):
    return not os.path.exists(dst) or os.path.getmtime(dst) < os.path.getmtime(src)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder", help='the "The Static Rift" folder (with Images and UI)')
    ap.add_argument("--size", type=int, default=160, help="card thumbnail size in pixels (160)")
    ap.add_argument("--quality", type=int, default=74, help="JPEG quality (74)")
    ap.add_argument("--force", action="store_true", help="redo pictures that are already there")
    args = ap.parse_args()
    root = args.folder
    images, ui, phone = (os.path.join(root, d) for d in ("Images", "UI", "Phone"))
    if not os.path.isdir(images):
        sys.exit(f"no Images folder in {root}")
    made = skipped = failed = 0
    total = 0
    for dirpath, _, files in os.walk(images):
        rel = os.path.relpath(dirpath, images)
        for name in sorted(files):
            if not name.lower().endswith(".qoi"):
                continue
            src = os.path.join(dirpath, name)
            dst = os.path.join(phone, rel, name[:-4] + ".jpg")
            if not args.force and not newer(src, dst):
                skipped += 1
                total += os.path.getsize(dst)
                continue
            try:
                thumb = card_thumb(read_qoi(src), args.size)
            except Exception as e:  # noqa: BLE001
                print(f"  skipped {src}: {e}")
                failed += 1
                continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            thumb.save(dst, "JPEG", quality=args.quality, optimize=True)
            total += os.path.getsize(dst)
            made += 1
    extras = 0
    for sub, size in (("elements", 64), ("games", None)):
        src_dir = os.path.join(ui, sub)
        if not os.path.isdir(src_dir):
            continue
        for name in sorted(os.listdir(src_dir)):
            if not name.lower().endswith(".qoi"):
                continue
            src = os.path.join(src_dir, name)
            dst = os.path.join(phone, sub, name[:-4] + ".png")
            if args.force or newer(src, dst):
                img = read_qoi(src)
                if size:
                    img = img.resize((size, size), Image.LANCZOS)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                img.save(dst, "PNG", optimize=True)
            total += os.path.getsize(dst)
            extras += 1
    bg = os.path.join(ui, "background.qoi")
    if os.path.exists(bg):
        dst = os.path.join(phone, "background.jpg")
        if args.force or newer(bg, dst):
            os.makedirs(phone, exist_ok=True)
            flatten(read_qoi(bg)).save(dst, "JPEG", quality=70, optimize=True)
        total += os.path.getsize(dst)
        extras += 1
    print(f"Phone pictures: {made} made, {skipped} already there, {failed} skipped, {extras} icons/logos/"
          f"background; {total / 1048576:.1f} MB in {phone}")


if __name__ == "__main__":
    main()
