"""Save what the five screens show right now as one PNG.

    python firmware/tools/screenshot.py <device-ip> [out.png] [--scale N] [--screen S]

Each screen comes from GET /api/screen?s=N (128x128 big-endian RGB565) and is enlarged with
nearest-neighbour, so every device pixel stays a sharp square. Needs Pillow.
"""
from __future__ import annotations

import argparse
import urllib.request

from PIL import Image


def grab(ip: str, s: int) -> Image.Image:
    raw = urllib.request.urlopen(f"http://{ip}/api/screen?s={s}", timeout=10).read()
    im = Image.new("RGB", (128, 128))
    px = []
    for i in range(0, len(raw), 2):
        v = raw[i] << 8 | raw[i + 1]
        px.append(((v >> 11) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31))
    im.putdata(px)
    return im


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("ip")
    ap.add_argument("out", nargs="?", default="screens.png")
    ap.add_argument("--scale", type=int, default=3)
    ap.add_argument("--screen", type=int, default=None, help="one screen (0..4) instead of all five")
    a = ap.parse_args()
    screens = [a.screen] if a.screen is not None else list(range(5))
    k, gap = a.scale, 8 * a.scale
    strip = Image.new("RGB", (len(screens) * 128 * k + (len(screens) - 1) * gap, 128 * k), (24, 26, 28))
    for n, s in enumerate(screens):
        strip.paste(grab(a.ip, s).resize((128 * k, 128 * k), Image.NEAREST), (n * (128 * k + gap), 0))
    strip.save(a.out)
    print(f"saved {a.out}")


if __name__ == "__main__":
    main()
