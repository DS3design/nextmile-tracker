"""Laser-cut layered case for the ESP32-2432S028R (CYD), 3 mm basswood/plywood.

Outputs case/nextmile_case.svg (all parts on one sheet). Red hairlines = cut, black fills = engrave.
Board dimensions are from photos of Don's 2-USB boards (ESP32-2432S028, USB-C + micro-USB) against a tape measure,
so they are good to about +/-1 mm. Do a cardboard test cut before cutting wood.
"""
import os, math
import numpy as np
from PIL import Image
import potrace
from shapely.geometry import box, Point, Polygon
from shapely.affinity import rotate, translate
from shapely.ops import unary_union
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.varLib import instancer

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')

# ---- material ----
T = 3.0          # sheet thickness
KERF = 0.15      # beam width; outlines grow / holes shrink by KERF/2 so parts come out to size

# ---- board (measured from photos, +/-1 mm), front view in landscape: antenna end on the left, USB and display ribbon on the right ----
PCB_W, PCB_H = 87.0, 50.5
HOLE_D = 3.2                      # not used by the case bolts, the board floats in its pocket
GLASS_W, GLASS_H, GLASS_X = 70.5, 50.5, 8.5    # display module outline, x from PCB left edge
VIEW_W, VIEW_H = 58.5, 44.0       # visible screen area (2.8" panel is 57.6 x 43.2)
VIEW_X = GLASS_X + (GLASS_W - VIEW_W) / 2 - 1.0
USB_Y, USB_W = PCB_H / 2, 15.0    # USB-C on the right short edge, about centred (micro-USB stays covered)
RST_X, RST_Y = 10.0, 10.0         # reset button (BOOT is just below it), front-view coordinates
LDR_X, LDR_Y = 4.2, 38.1          # light sensor (R21, white 3.4 x 5.1 mm part) on the front, left of the glass
LDR_W, LDR_H = 3.8, 5.5
LED_X, LED_Y = 29.0, 38.0         # RGB LED on the back, front-view coordinates

# ---- case ----
SIDE, TOP, BOTTOM = 8.0, 8.0, 18.0   # frame borders around the board; engraving lives in the bottom band
R = 4.0                              # corner radius
CLEAR = 0.6                          # play around board/glass
BOLT_D, BOLT_INSET = 3.3, 4.2        # M3 through-holes in the frame corners
OUT_W, OUT_H = PCB_W + 2 * SIDE, PCB_H + TOP + BOTTOM
BX, BY = SIDE, TOP                   # board origin inside the frame (y down)
TILT = 15                            # stand lean-back in degrees
SPACERS = 2                          # extra 3 mm rings behind the board for component clearance

# ---- stylus: 3 mm shaft, 75 mm long, slides into a tunnel in the bottom band of layer 4 from the left edge ----
PEN_D, PEN_L = 3.0, 75.0
PEN_Y = BY + PCB_H + 6.0             # tunnel centre; clear of the board pocket above and the bolt holes below
PEN_DEPTH = PEN_L - 5.0              # 5 mm sticks out so it is easy to grab
PEN_GRIP = 0.3                       # bump on the spring wall that squeezes the shaft
PEN_WALL, PEN_SLOT = 1.2, 0.8        # spring wall thickness and the relief slot behind it

CUT = 'stroke="#ff0000" stroke-width="0.1" fill="none"'
ENG = 'fill="#000000" fill-rule="evenodd" stroke="none"'


def rrect(x, y, w, h, r):
    return box(x + r, y, x + w - r, y + h).union(box(x, y + r, x + w, y + h - r)).union(
        unary_union([Point(px, py).buffer(r, 64) for px in (x + r, x + w - r) for py in (y + r, y + h - r)]))


def bolts():
    return [Point(x, y).buffer(BOLT_D / 2, 32) for x in (BOLT_INSET, OUT_W - BOLT_INSET)
            for y in (BOLT_INSET, OUT_H - BOLT_INSET)]


def kerf(poly):
    """Exterior grows, interiors shrink by half the kerf (buffer does both)."""
    return poly.buffer(KERF / 2, join_style='mitre')


def path_of(poly):
    polys = getattr(poly, 'geoms', [poly])
    d = []
    for p in polys:
        for ring in [p.exterior, *p.interiors]:
            pts = list(ring.coords)
            d.append('M' + ' L'.join(f'{x:.3f},{y:.3f}' for x, y in pts) + ' Z')
    return ' '.join(d)


_font = None


def text_path(s, x, y, size, anchor='middle', spacing=0.12):
    """Oswald SemiBold text as a filled path. (x, y) is the baseline anchor in mm."""
    global _font
    if _font is None:
        _font = instancer.instantiateVariableFont(TTFont(os.path.join(HERE, 'Oswald.ttf')), {'wght': 600})
    gs, cmap = _font.getGlyphSet(), _font.getBestCmap()
    upm = _font['head'].unitsPerEm
    sc = size / upm
    names = [cmap[ord(ch)] for ch in s]
    widths = [gs[n].width * sc + size * spacing for n in names]
    total = sum(widths) - size * spacing
    x0 = x - (total / 2 if anchor == 'middle' else 0)
    pen = SVGPathPen(gs)
    for n, w in zip(names, widths):
        gs[n].draw(TransformPen(pen, (sc, 0, 0, -sc, x0, y)))
        x0 += w
    return pen.getCommands()


def trace(png, mask_fn, x, y, w, h):
    """Bitmap logo -> filled path fitted into the (x, y, w, h) box, keeping aspect."""
    im = Image.open(png).convert('RGBA')
    if im.width > 1600:
        im = im.resize((1600, im.height * 1600 // im.width), Image.LANCZOS)
    a = np.array(im)
    m = mask_fn(a)
    ys, xs = np.nonzero(m)
    m = m[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    ih, iw = m.shape
    s = min(w / iw, h / ih)
    ox, oy = x + (w - iw * s) / 2, y + (h - ih * s) / 2
    plist = potrace.Bitmap(~m).trace(  # potracer traces the False pixels
        turdsize=int(4 / s / 10) + 2, alphamax=1.0, opttolerance=0.2)
    f = lambda p: f'{ox + p.x * s:.3f},{oy + p.y * s:.3f}'
    d = []
    for curve in plist:
        d.append('M' + f(curve.start_point))
        for seg in curve.segments:
            if seg.is_corner:
                d.append('L' + f(seg.c) + ' L' + f(seg.end_point))
            else:
                d.append('C' + f(seg.c1) + ' ' + f(seg.c2) + ' ' + f(seg.end_point))
        d.append('Z')
    return ' '.join(d)


def plate(inner=None, extra=()):
    shape = rrect(0, 0, OUT_W, OUT_H, R)
    for b in bolts():
        shape = shape.difference(b)
    if inner is not None:
        shape = shape.difference(inner)
    for e in extra:
        shape = shape.difference(e)
    return kerf(shape)


def usb_notch():
    return box(BX + PCB_W - 1, BY + USB_Y - USB_W / 2, OUT_W + 1, BY + USB_Y + USB_W / 2)


def pen_tunnel():
    """Tunnel with a flared mouth and a sprung bump halfway in that holds the stylus."""
    top, bot = PEN_Y - PEN_D / 2, PEN_Y + PEN_D / 2
    mid = PEN_DEPTH / 2
    a, b, c, d = mid - 9, mid - 5, mid + 5, mid + 9       # ramp up, flat grip, ramp down
    g = bot - PEN_GRIP
    tunnel = Polygon([(-1, top - 0.6), (0, top - 0.5), (2.5, top), (PEN_DEPTH, top), (PEN_DEPTH, bot),
                      (d, bot), (c, g), (b, g), (a, bot), (2.5, bot), (0, bot + 0.5), (-1, bot + 0.6)])
    slot = rrect(a - 3, bot + PEN_WALL, (d + 3) - (a - 3), PEN_SLOT, PEN_SLOT / 2)
    return [tunnel, slot]


def parts():
    P = []
    nm = os.path.join(ROOT, 'assets', 'nextmile_logo.png')
    ds3 = os.path.join(ROOT, 'assets', 'ds3design_logo.png')
    dark = lambda a: (a[..., 3] > 128) & (a[..., :3].max(axis=2) < 110)
    opaque = lambda a: a[..., 3] > 110

    # 1 front: screen window, Next Mile logo + text in the bottom band
    window = rrect(BX + VIEW_X, BY + (PCB_H - VIEW_H) / 2, VIEW_W, VIEW_H, 1.0)
    band_y = BY + PCB_H + 2
    eng = [trace(nm, dark, SIDE + 2, band_y - 1, 26, BOTTOM - 2),
           text_path('NEXT MILE TRACKER', OUT_W / 2 + 13, band_y + 6.5, 4.8),
           text_path('PACEGIVING 2026', OUT_W / 2 + 13, band_y + 13.0, 4.3)]
    ldr = box(BX + LDR_X - LDR_W / 2, BY + LDR_Y - LDR_H / 2,
              BX + LDR_X + LDR_W / 2, BY + LDR_Y + LDR_H / 2)   # the whole sensor shows through the frame
    P.append(('1 FRONT', plate(window, [ldr]), eng))

    # 2 bezel: holds the display glass
    glass = box(BX + GLASS_X - CLEAR, BY - CLEAR, BX + GLASS_X + GLASS_W + CLEAR, BY + PCB_H + CLEAR)
    P.append(('2 BEZEL', plate(glass, [ldr]), []))

    # 3 pocket + spacers: the board itself and parts on its back, with the USB notch
    pcb = box(BX - CLEAR, BY - CLEAR, BX + PCB_W + CLEAR, BY + PCB_H + CLEAR)
    P.append(('3 POCKET', plate(pcb, [usb_notch()]), []))
    for i in range(SPACERS):
        P.append((f'{4 + i} SPACER', plate(pcb, [usb_notch(), *(pen_tunnel() if i == 0 else [])]), []))

    # back: reset pinhole, vents, DS3design logo + hint (engraved on the outside face, mirrored back view)
    rst = Point(OUT_W - (BX + RST_X), BY + RST_Y).buffer(1.2, 24)   # mirrored: we see the back from behind
    vents = [rrect(OUT_W / 2 - 25 + i * 10, BY + 8, 3, 22, 1.5) for i in range(6)]
    eng = [trace(ds3, opaque, OUT_W / 2 - 20, BY + PCB_H - 6, 40, 12),   # below the LED hole
           text_path('RESET', OUT_W - (BX + RST_X), BY + RST_Y + 6, 2.6),
           text_path('HOLD SCREEN 10 SEC TO ERASE & START OVER', OUT_W / 2, OUT_H - 6, 3.0, spacing=0.08)]
    led = Point(OUT_W - (BX + LED_X), BY + LED_Y).buffer(2.5, 32)   # colored light shows through
    P.append((f'{4 + SPACERS} BACK', plate(None, [rst, led, *vents]), eng))

    # stand feet: the case's bottom edge drops into a notch tilted TILT degrees back
    stack = T * (4 + SPACERS) + 0.3
    foot = Polygon([(0, 0), (46, 0), (46, 4), (36, 18), (14, 18), (0, 6)])
    notch = translate(rotate(box(-stack / 2, 0, stack / 2, 30), -TILT, origin=(0, 0)), 24, 7)
    foot = kerf(foot.difference(notch))
    foot = translate(foot.__class__(foot.exterior.coords, [i.coords for i in foot.interiors]), 0, 0)
    foot = Polygon([(x, 18 - y) for x, y in foot.exterior.coords])  # flip so the flat side is down
    P.append(('STAND FOOT x2', foot, []))
    P.append(('STAND FOOT x2', foot, []))
    return P


SHEET = 216.0   # usable bed per sheet (xTool F2 Ultra class lasers, ~220 mm square)


def pack(P, gap=3.0, label=5.0):
    """Shelf-pack parts into as many SHEET x SHEET sheets as needed."""
    sheets, cur, x, y, rowh = [], [], gap, gap + label, 0
    for name, poly, eng in P:
        minx, miny, maxx, maxy = poly.bounds
        w, h = maxx - minx, maxy - miny
        if x + w + gap > SHEET:
            x, y, rowh = gap, y + rowh + gap + label, 0
        if y + h + gap > SHEET:
            sheets.append(cur)
            cur, x, y, rowh = [], gap, gap + label, 0
        cur.append((name, poly, eng, x - minx, y - miny))
        x += w + gap
        rowh = max(rowh, h)
    sheets.append(cur)
    return sheets


BED = 220.0    # xTool F2 Ultra work area; single-piece files are centered on it, where the beam is straightest


def centered(name, polys, eng, gap=3.0):
    """One piece (or a set of identical small ones side by side) in the middle of the bed."""
    w = sum(p.bounds[2] - p.bounds[0] for p in polys) + gap * (len(polys) - 1)
    h = max(p.bounds[3] - p.bounds[1] for p in polys)
    x, y, out = (BED - w) / 2, (BED - h) / 2, []
    for p in polys:
        out.append((name, p, eng, x - p.bounds[0], y - p.bounds[1]))
        x += p.bounds[2] - p.bounds[0] + gap
    return out


def write(sheet, dst, W=SHEET):
    H = W
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W:.0f}mm" height="{H:.0f}mm" viewBox="0 0 {W:.0f} {H:.0f}">',
           '<!-- Next Mile Tracker case. Red = cut, black = engrave. Grey labels are notes: hide or delete before cutting. -->',
           '<g id="notes" fill="#999999" font-family="sans-serif" font-size="3.2">']
    for name, poly, _, dx, dy in sheet:
        out.append(f'<text x="{dx + poly.bounds[0]:.2f}" y="{dy + poly.bounds[1] - 1.5:.2f}">{name}</text>')
    out.append('</g><g id="engrave">')
    for _, _, eng, dx, dy in sheet:
        for d in eng:
            out.append(f'<path transform="translate({dx:.3f},{dy:.3f})" {ENG} d="{d}"/>')
    out.append('</g><g id="cut">')
    for _, poly, _, dx, dy in sheet:
        out.append(f'<path transform="translate({dx:.3f},{dy:.3f})" {CUT} d="{path_of(poly)}"/>')
    out.append('</g></svg>')
    open(dst, 'w').write('\n'.join(out))


def main():
    sheets = pack(parts())
    for i, sheet in enumerate(sheets, 1):
        dst = os.path.join(ROOT, 'case', f'nextmile_case_sheet{i}.svg')
        write(sheet, dst)
        print('wrote', dst, [n for n, *_ in sheet])
    P = parts()
    feet = [p for n, p, _ in P if n.startswith('STAND FOOT')]
    singles = [(n, [p], e) for n, p, e in P if not n.startswith('STAND FOOT')]
    singles.append(('STAND FEET x4', feet * 2, []))
    os.makedirs(os.path.join(ROOT, 'case', 'pieces'), exist_ok=True)
    for name, polys, eng in singles:
        slug = name.lower().replace(' ', '_')
        write(centered(name, polys, eng), os.path.join(ROOT, 'case', 'pieces', f'{slug}.svg'), BED)
    print('wrote', len(singles), 'single-piece files in case/pieces')
    print(f'case {OUT_W:.0f} x {OUT_H:.0f} x {T * (4 + SPACERS):.0f} mm')


if __name__ == '__main__':
    main()
