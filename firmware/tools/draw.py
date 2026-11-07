"""Drawing helpers for the asset generators (gen_fonts.py, gen_assets.py).

- font(): Inter (UI) and Orbitron (display) from assets/fonts, at whole-pixel sizes.
- Canvas: pixel-exact shapes on a small image. Pixel i spans [i, i+1); each shape is drawn
  4x into a patch aligned to whole pixels and box-filtered down, so straight edges on integer
  coordinates stay sharp and only curves get anti-aliasing.
- weather(): flat weather icons for Open-Meteo's WMO codes.
"""
from __future__ import annotations

import math
from functools import lru_cache
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FONTS_DIR = Path(__file__).resolve().parents[2] / "assets" / "fonts"
SS = 4

CANVAS = (0, 0, 0)                     # the LCDs' page: true black
WARNING = (251, 191, 36)
SUN, MOON = WARNING, (230, 232, 236)
CLOUD, CLOUD_DARK = (207, 208, 210), (122, 123, 126)
RAIN, SNOW = (120, 190, 217), (245, 246, 247)

INTER = {400: "Inter-Regular.ttf", 500: "Inter-Medium.ttf", 600: "Inter-SemiBold.ttf", 700: "Inter-Bold.ttf"}


@lru_cache(maxsize=64)
def font(size: int, weight: int = 400, family: str = "ui") -> ImageFont.FreeTypeFont:
    """family "ui" = Inter (static weights), "brand" = Orbitron (one variable file)."""
    if family == "brand":
        f = ImageFont.truetype(str(FONTS_DIR / "Orbitron.ttf"), int(size))
        f.set_variation_by_axes([weight])
        return f
    return ImageFont.truetype(str(FONTS_DIR / INTER[weight]), int(size))


def _rgba(c):
    return c if len(c) == 4 else (*c, 255)


class Canvas:
    def __init__(self, w: int, h: int, bg=CANVAS):
        self.w, self.h = w, h
        self.im = Image.new("RGBA", (w, h), _rgba(bg))

    def _patch(self, x0, y0, x1, y1, draw):
        px0, py0 = math.floor(x0) - 1, math.floor(y0) - 1
        px1, py1 = math.ceil(x1) + 1, math.ceil(y1) + 1
        w, h = px1 - px0, py1 - py0
        layer = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
        draw(ImageDraw.Draw(layer), lambda x, y: ((x - px0) * SS, (y - py0) * SS))
        self.im.alpha_composite(layer.resize((w, h), Image.BOX), (px0, py0))

    def rect(self, x0, y0, x1, y1, fill, r=0.0):
        def draw(d, t):
            a, b = t(x0, y0)
            c, e = t(x1, y1)
            d.rounded_rectangle([a, b, c - 1, e - 1], radius=r * SS, fill=_rgba(fill))
        self._patch(x0, y0, x1, y1, draw)

    def circle(self, cx, cy, rad, fill):
        def draw(d, t):
            a, b = t(cx - rad, cy - rad)
            c, e = t(cx + rad, cy + rad)
            d.ellipse([a, b, c - 1, e - 1], fill=_rgba(fill))
        self._patch(cx - rad, cy - rad, cx + rad, cy + rad, draw)

    def line(self, pts, fill, width=1.0):
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        self._patch(min(xs) - width, min(ys) - width, max(xs) + width, max(ys) + width,
                    lambda d, t: d.line([t(x, y) for x, y in pts], fill=_rgba(fill),
                                        width=max(1, round(width * SS)), joint="curve"))

    def polygon(self, pts, fill):
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        self._patch(min(xs), min(ys), max(xs), max(ys),
                    lambda d, t: d.polygon([t(x, y) for x, y in pts], fill=_rgba(fill)))

    def finish(self) -> Image.Image:
        return self.im.convert("RGB")


# --- weather icons: (cx, cy) is the centre, s the icon's size in px ------------------------------
def _sun(c, cx, cy, s):
    r = s * 0.22
    c.circle(cx, cy, r, SUN)
    for i in range(8):
        a = math.pi / 4 * i
        c.line([(cx + math.cos(a) * r * 1.45, cy + math.sin(a) * r * 1.45),
                (cx + math.cos(a) * r * 1.95, cy + math.sin(a) * r * 1.95)], SUN, s * 0.06)


def _moon(c, cx, cy, s, bg=CANVAS):
    r = s * 0.26
    c.circle(cx, cy, r, MOON)
    c.circle(cx + r * 0.55, cy - r * 0.35, r * 0.85, bg)


def _cloud(c, cx, cy, s, color=CLOUD):
    w = s * 0.78
    x0, y1 = cx - w / 2, cy + s * 0.16
    c.circle(cx - w * 0.2, cy, s * 0.16, color)
    c.circle(cx + w * 0.05, cy - s * 0.08, s * 0.22, color)
    c.circle(cx + w * 0.28, cy + s * 0.02, s * 0.14, color)
    c.rect(x0 + s * 0.04, cy, x0 + w - s * 0.02, y1, color, r=s * 0.14)


def _drops(c, cx, cy, s, n=3):
    for i in range(n):
        x = cx - s * 0.22 + i * s * 0.22
        c.line([(x + s * 0.04, cy), (x - s * 0.04, cy + s * 0.16)], RAIN, s * 0.06)


def _flakes(c, cx, cy, s, n=3):
    for i in range(n):
        c.circle(cx - s * 0.22 + i * s * 0.22, cy + s * 0.08, s * 0.045, SNOW)


def _bolt(c, cx, cy, s):
    c.polygon([(cx + s * 0.04, cy - s * 0.02), (cx - s * 0.1, cy + s * 0.16), (cx, cy + s * 0.16),
               (cx - s * 0.06, cy + s * 0.32), (cx + s * 0.12, cy + s * 0.1), (cx + s * 0.02, cy + s * 0.1)], WARNING)


def _fog(c, cx, cy, s):
    for i, dx in enumerate((0, 0.08, -0.04)):
        y = cy - s * 0.12 + i * s * 0.14
        c.line([(cx - s * 0.32 + dx * s, y), (cx + s * 0.32 + dx * s, y)], CLOUD, s * 0.07)


def weather(c: Canvas, cx, cy, s, code: int, is_day: bool = True):
    """A WMO weather code (as Open-Meteo reports it) as an icon."""
    sky = (lambda x, y, sz: _sun(c, x, y, sz)) if is_day else (lambda x, y, sz: _moon(c, x, y, sz))
    if code == 0:
        sky(cx, cy, s)
    elif code in (1, 2):
        sky(cx - s * 0.16, cy - s * 0.14, s * 0.8)
        _cloud(c, cx + s * 0.04, cy + s * 0.06, s * 0.9)
    elif code == 3:
        _cloud(c, cx + s * 0.1, cy - s * 0.1, s * 0.7, CLOUD_DARK)
        _cloud(c, cx - s * 0.02, cy + s * 0.04, s)
    elif code in (45, 48):
        _fog(c, cx, cy, s)
    elif code in (71, 73, 75, 77, 85, 86):
        _cloud(c, cx, cy - s * 0.1, s)
        _flakes(c, cx, cy + s * 0.14, s)
    elif code in (95, 96, 99):
        _cloud(c, cx, cy - s * 0.12, s, CLOUD_DARK)
        _bolt(c, cx, cy, s)
    elif code >= 51:                       # drizzle, rain, showers
        if code in (80, 81, 82) and is_day:
            _sun(c, cx - s * 0.2, cy - s * 0.2, s * 0.6)
        _cloud(c, cx, cy - s * 0.1, s)
        _drops(c, cx, cy + s * 0.12, s, 3 if code not in (51, 53, 55) else 2)
    else:
        _cloud(c, cx, cy, s)
