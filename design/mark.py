"""The Pentadeck mark: one geometry, drawn as SVG paths and as PIL images.

A pentagon seen from above, in concentric rings around a courtyard: five wings make the outer
ring, one per screen, lit from the upper left so the mark reads with some depth. Inside them, a
continuous ring and a small pentagonal courtyard. Everything is built from straight lines and
fillets on a 512 grid, so the SVG and the bitmaps (favicon, touch icon, the emblem on the
device's clock screen) are the same shape.

    from design.mark import svg_mark, render
    render(64)                      # a 64x64 RGBA PIL image
    render(36, lit=[...])           # a frame of the device's emblem animation
"""
from __future__ import annotations

import math

# --- the brand ramp (teal), dark to light -------------------------------------------------------
T900, T800, T700, T600, T500, T400, T300 = "#164E63", "#1B5E78", "#206F8D", "#2A8CB2", "#42A7CD", "#78BED9", "#A8D5E6"
# the five wings clockwise from the top vertex: upper right, lower right, bottom, lower left, upper left
WING_COLORS = (T500, T700, T800, T600, T400)     # light from the upper left
RING_COLOR = T800
COURT_COLOR = T400

# --- geometry, on a 512 x 512 grid ---------------------------------------------------------------
BOX = 512
R_OUT = 262.0                 # circumradius of the outer edge (vertex up)
A_OUT = R_OUT * math.cos(math.radians(36))       # its apothem
WING = 84.0                   # the wings' depth, measured square to the sides
GAP = 14.0                    # the joints between wings, along the corners' bisectors
RING_GAP = 18.0               # between the wings and the inner ring
RING = 30.0                   # the inner ring's depth
COURT_GAP = 18.0              # between the inner ring and the courtyard
FILLET_OUT, FILLET_IN, FILLET_RING, FILLET_COURT = 18.0, 7.0, 10.0, 8.0

CX = BOX / 2
CY = BOX / 2 + (R_OUT - A_OUT) / 2               # centre the pentagon's bounding box
INK = (CX - R_OUT * math.sin(math.radians(72)), CY - R_OUT, CX + R_OUT * math.sin(math.radians(72)), CY + A_OUT)


def _e(theta):                # unit vector at a clock angle (0 = up, clockwise; y down)
    a = math.radians(theta)
    return (math.sin(a), -math.cos(a))


def _t(theta):                # the clockwise tangent at a clock angle
    a = math.radians(theta)
    return (math.cos(a), math.sin(a))


def _meet(n1, d1, n2, d2):
    """The point p (relative to the centre) with n1.p = d1 and n2.p = d2."""
    det = n1[0] * n2[1] - n1[1] * n2[0]
    return ((d1 * n2[1] - d2 * n1[1]) / det, (n1[0] * d2 - n2[0] * d1) / det)


def _abs(p):
    return (CX + p[0], CY + p[1])


def pentagon(apothem):
    """The corners of a regular pentagon (vertex up) with this apothem, clockwise from the top."""
    r = apothem / math.cos(math.radians(36))
    return [_abs(_mul(r, _e(72 * k))) for k in range(5)]


def _mul(k, v):
    return (k * v[0], k * v[1])


def wing(i):
    """Corners of wing i (0 = upper right, clockwise), clockwise on screen."""
    n = _e(72 * i + 36)                      # the side's outward normal
    a0, a1 = A_OUT, A_OUT - WING
    ta = _t(72 * i)                          # joint at vertex i: the wing is where ta.p >= GAP/2
    tb = _mul(-1, _t(72 * (i + 1)))          # joint at vertex i+1
    return [_abs(_meet(n, a0, ta, GAP / 2)), _abs(_meet(n, a0, tb, GAP / 2)),
            _abs(_meet(n, a1, tb, GAP / 2)), _abs(_meet(n, a1, ta, GAP / 2))]


def _rounded(pts, radii):
    """A convex polygon with each corner filleted: path pieces ('M', p) / ('L', p) /
    ('A', radius, centre, end, None)."""
    n = len(pts)
    out = []
    for i in range(n):
        p, a, b = pts[i], pts[i - 1], pts[(i + 1) % n]
        u1 = _unit((a[0] - p[0], a[1] - p[1]))
        u2 = _unit((b[0] - p[0], b[1] - p[1]))
        th = math.acos(max(-1.0, min(1.0, u1[0] * u2[0] + u1[1] * u2[1])))
        r = radii[i]
        d = r / math.tan(th / 2)
        t1 = (p[0] + u1[0] * d, p[1] + u1[1] * d)
        t2 = (p[0] + u2[0] * d, p[1] + u2[1] * d)
        bis = _unit((u1[0] + u2[0], u1[1] + u2[1]))
        c = (p[0] + bis[0] * r / math.sin(th / 2), p[1] + bis[1] * r / math.sin(th / 2))
        out.append(("M" if i == 0 else "L", t1))
        out.append(("A", r, c, t2, None))
    return out


def _unit(v):
    L = math.hypot(v[0], v[1])
    return (v[0] / L, v[1] / L)


def shapes(colors=WING_COLORS, ring=RING_COLOR, court=COURT_COLOR):
    """(list of outlines, colour) for each filled shape. A shape with two outlines is a ring
    (the second outline is the hole)."""
    out = []
    for i in range(5):
        out.append(([_rounded(wing(i), [FILLET_OUT, FILLET_OUT, FILLET_IN, FILLET_IN])], colors[i]))
    a_ring = A_OUT - WING - RING_GAP
    if RING > 0:
        out.append(([_rounded(pentagon(a_ring), [FILLET_RING] * 5),
                     _rounded(pentagon(a_ring - RING), [FILLET_RING * 0.4] * 5)], ring))
    a_court = a_ring - RING - (COURT_GAP if RING > 0 else 0)
    out.append(([_rounded(pentagon(a_court), [FILLET_COURT] * 5)], court))
    return out


# --- SVG ---------------------------------------------------------------------------------------------
def _f(x):
    s = f"{x:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def _sweep(centre, p0, p1):
    z = (p0[0] - centre[0]) * (p1[1] - centre[1]) - (p0[1] - centre[1]) * (p1[0] - centre[0])
    return 1 if z > 0 else 0


def path_d(pieces, ox=0.0, oy=0.0, scale=1.0):
    out, cur = [], None
    P = lambda p: f"{_f(ox + scale * p[0])} {_f(oy + scale * p[1])}"
    for pc in pieces:
        if pc[0] in ("M", "L"):
            out.append(f"{pc[0]} {P(pc[1])}"); cur = pc[1]
            continue
        radius, centre, end = pc[1], pc[2], pc[3]
        out.append(f"A {_f(scale * radius)} {_f(scale * radius)} 0 0 {_sweep(centre, cur, end)} {P(end)}"); cur = end
    return " ".join(out) + " Z"


def svg_parts(x=0.0, y=0.0, size=BOX, **colors):
    """The mark as SVG elements, drawn into the square (x, y, size)."""
    k = size / BOX
    els = []
    for outlines, col in shapes(**colors):
        d = " ".join(path_d(o, x, y, k) for o in outlines)
        rule = ' fill-rule="evenodd"' if len(outlines) > 1 else ""
        els.append(f'<path d="{d}"{rule} fill="{col}"/>')
    return els


def svg_mark(comment: str = "") -> str:
    note = f"  <!-- {comment} -->\n" if comment else ""
    body = "\n  ".join(svg_parts())
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {BOX} {BOX}">\n{note}  {body}\n</svg>\n'


# --- bitmaps -------------------------------------------------------------------------------------------
def _sample(pieces, step=1.5):
    pts, cur = [], None
    for pc in pieces:
        if pc[0] in ("M", "L"):
            cur = pc[1]; pts.append(cur); continue
        radius, centre, end = pc[1], pc[2], pc[3]
        a0 = math.atan2(cur[1] - centre[1], cur[0] - centre[0])
        a1 = math.atan2(end[1] - centre[1], end[0] - centre[0])
        da = (a1 - a0 + math.pi) % (2 * math.pi) - math.pi
        n = max(2, int(abs(da) * radius / step))
        for j in range(1, n + 1):
            a = a0 + da * j / n
            pts.append((centre[0] + radius * math.cos(a), centre[1] + radius * math.sin(a)))
        cur = end
    return pts


def rgb(hexs):
    return tuple(int(hexs[i:i + 2], 16) for i in (1, 3, 5))


def mix(c1, c2, k):
    a, b = rgb(c1), rgb(c2)
    return "#" + "".join(f"{round(x + (y - x) * k):02X}" for x, y in zip(a, b))


def render(px: int, lit=None, bg=None, ss: int = 8, dim_to: str = "#0E2A35", **colors):
    """The mark as a px x px RGBA image (transparent unless bg is given).

    lit: per wing (clockwise from the upper right) 0..1, 1 = its colour, 0 = dimmed towards
    dim_to; default all lit. Drawn ss times larger, then reduced."""
    from PIL import Image, ImageDraw

    S = px * ss
    k = S / BOX
    im = Image.new("RGBA", (S, S), rgb(bg) + (255,) if bg else (0, 0, 0, 0))
    lit = lit if lit is not None else [1.0] * 5
    for idx, (outlines, col) in enumerate(shapes(**colors)):
        if idx < 5:
            col = mix(dim_to, col, max(0.0, min(1.0, lit[idx])))
        layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        for j, o in enumerate(outlines):
            pts = [(k * x, k * y) for x, y in _sample(o, step=1.0 / k)]
            d.polygon(pts, fill=rgb(col) + (255,) if j == 0 else (0, 0, 0, 0))
        im.alpha_composite(layer)
    while im.width // 2 >= px and im.width > px:
        im = im.resize((im.width // 2, im.height // 2), Image.LANCZOS)
    if im.width != px:
        im = im.resize((px, px), Image.LANCZOS)
    return im
