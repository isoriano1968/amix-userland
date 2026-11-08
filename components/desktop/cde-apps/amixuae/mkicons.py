#!/usr/bin/env python3
"""mkicons.py -- amixuae's icons as CDE XPMs, large/medium/tiny (48, 32, 16):

  AmixUAE   the application: a beige wedge computer with its floppy slot, in
            front of a monitor with a blue desktop (our own drawing, no
            trademarked logo)
  AmigaADF  a floppy image: a 3.5" disk, blue label stripe
  AmigaHDF  a hard file: a drive with a green light
  AmigaROM  a Kickstart ROM: a chip with legs

  python3 mkicons.py           the XPMs into icons/
  python3 mkicons.py PREVIEW   also a PNG sheet (needs PIL) to look at
"""
import os
import sys

PAL = {
    '.': 'None',
    'K': '#1A1A1A',  # outline
    'g': '#7C8088', 'G': '#A9ADB5', 'h': '#D5D8DE',            # monitor greys
    'b': '#2C5FB8', 'B': '#4F86E0', 'w': '#F4F4F4', 'o': '#F08A24',  # desktop
    'c': '#BDB39A', 'C': '#DDD4BC', 'e': '#E9E3D0', 'd': '#8F866F',  # beige case
    'k': '#4A4A4A', 'l': '#6E6E6E',                           # keys
    'r': '#D23B2E', 'v': '#33C24A',                           # lights
    'f': '#2A2F3A', 'F': '#3F4656', 'm': '#B9BEC6', 'M': '#E2E5EA',  # floppy
    's': '#5B6170', 'S': '#8E95A6',                           # drive
    'p': '#C9A64A', 'P': '#6B5A2A',                           # chip legs
}


class Canvas:
    def __init__(self, n):
        self.n = n
        self.g = [['.'] * n for _ in range(n)]

    def px(self, x, y, c):
        if 0 <= x < self.n and 0 <= y < self.n:
            self.g[y][x] = c

    def rect(self, x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.px(x, y, c)

    def box(self, x0, y0, x1, y1, fill, edge='K'):
        self.rect(x0, y0, x1, y1, edge)
        if x1 - x0 > 1 and y1 - y0 > 1:
            self.rect(x0 + 1, y0 + 1, x1 - 1, y1 - 1, fill)

    def s(self, v):
        """a length given for 48 pixels, at this size (at least 1)"""
        return max(1, round(v * self.n / 48))


def app(n):
    c = Canvas(n)
    s = c.s
    # monitor, upper right
    mx0, my0, mx1, my1 = s(14), s(2), n - 1 - s(1), s(30)
    c.box(mx0, my0, mx1, my1, 'G')
    c.rect(mx0 + 1, my0 + 1, mx1 - 1, my0 + 1, 'h')            # light top edge
    sx0, sy0, sx1, sy1 = mx0 + s(3), my0 + s(3), mx1 - s(3), my1 - s(4)
    c.box(sx0, sy0, sx1, sy1, 'b')
    if n >= 32:
        c.rect(sx0 + 1, sy0 + 1, sx1 - 1, sy0 + s(3), 'w')     # title bar
        c.rect(sx1 - s(5), sy0 + 1, sx1 - 1, sy0 + s(3), 'B')  # its gadgets
        wx0, wy0 = sx0 + s(3), sy0 + s(6)                      # a window
        c.box(wx0, wy0, wx0 + s(11), wy0 + s(7), 'B', 'w')
        c.rect(wx0 + 1, wy0 + 1, wx0 + s(11) - 1, wy0 + 1, 'o')
    else:
        c.rect(sx0 + 1, sy0 + 1, sx1 - 1, sy0 + 1, 'w')
    c.rect(mx0 + s(14), my1 + 1, mx0 + s(20), my1 + s(2), 'g')  # neck
    # the wedge computer, lower left: sloped top, keyboard, floppy slot
    cx0, cy0, cx1, cy1 = 0, s(27), s(36), n - 1 - s(2)
    slope = s(5)
    for y in range(cy0, cy1 + 1):
        inset = max(0, slope - (y - cy0) * slope // max(1, (cy1 - cy0) // 2))
        for x in range(cx0 + inset, cx1 - inset // 2 + 1):
            edge = y in (cy0, cy1) or x in (cx0 + inset, cx1 - inset // 2)
            c.px(x, y, 'K' if edge else ('e' if y < cy0 + s(3) else 'C'))
    c.rect(cx0 + 1, cy1 - s(3), cx1 - 1, cy1 - 1, 'c')        # front face
    c.rect(cx0 + 1, cy1, cx1, cy1, 'd')
    # keys: rows of dark keys
    kx0, kx1 = cx0 + s(4), cx1 - s(9)
    if n >= 32:
        rows = [cy0 + s(4), cy0 + s(7), cy0 + s(10)]
        for i, y in enumerate(rows):
            x = kx0 + i * s(1)
            while x + s(2) <= kx1:
                c.rect(x, y, x + s(2) - 1, y + s(2) - 1, 'k')
                x += s(3)
        c.rect(kx0 + s(4), cy0 + s(13), kx1 - s(6), cy0 + s(14), 'l')   # space bar
    else:
        c.rect(kx0, cy0 + 3, kx1, cy0 + 3, 'k')
        c.rect(kx0 + 1, cy0 + 5, kx1 - 1, cy0 + 5, 'k')
    # floppy slot on the right side, power light on the front
    c.rect(cx1 - s(7), cy0 + s(6), cx1 - s(2), cy0 + s(6) + max(1, s(1)), 'K')
    c.px(cx0 + s(3), cy1 - s(2), 'v')
    c.px(cx0 + s(3) + 1, cy1 - s(2), 'v') if n >= 32 else None
    return c.g


def adf(n):
    c = Canvas(n)
    s = c.s
    x0, y0, x1, y1 = s(6), s(4), n - 1 - s(6), n - 1 - s(4)
    c.box(x0, y0, x1, y1, 'F')
    c.px(x1, y0, '.')                                  # the cut corner
    c.px(x1 - 1, y0, 'K')
    c.px(x1, y0 + 1, 'K')
    # metal shutter at the top with its window
    c.box(x0 + s(10), y0, x1 - s(8), y0 + s(13), 'm')
    c.rect(x0 + s(10) + 1, y0, x1 - s(8) - 1, y0, 'M')
    c.rect(x1 - s(14), y0 + s(3), x1 - s(11), y0 + s(10), 'f')
    # label: white with a blue stripe
    c.box(x0 + s(4), y1 - s(18), x1 - s(4), y1, 'w')
    c.rect(x0 + s(4) + 1, y1 - s(18) + 1, x1 - s(4) - 1, y1 - s(18) + s(3), 'B')
    if n >= 32:
        for i in range(3):
            y = y1 - s(11) + i * s(4)
            c.rect(x0 + s(7), y, x1 - s(9) - i * s(4), y, 'G')
    return c.g


def hdf(n):
    c = Canvas(n)
    s = c.s
    # a 3.5" drive seen from above and the front: top with a label, front with light
    x0, y0, x1, y1 = s(3), s(8), n - 1 - s(3), n - 1 - s(8)
    front = s(9)
    c.box(x0, y0, x1, y1, 'S')
    c.rect(x0 + 1, y0 + 1, x1 - 1, y1 - front - 1, 'm')           # top
    c.rect(x0 + 1, y0 + 1, x1 - 1, y0 + 1, 'M')
    if n >= 32:
        c.box(x0 + s(6), y0 + s(5), x1 - s(6), y1 - front - s(5), 'w', 'G')   # label
        c.rect(x0 + s(6) + 1, y0 + s(5) + 1, x1 - s(6) - 1, y0 + s(5) + s(3), 'B')
    c.rect(x0, y1 - front, x1, y1 - front, 'K')                   # the edge
    c.rect(x0 + 1, y1 - front + 1, x1 - 1, y1 - 1, 's')           # front
    c.rect(x1 - s(9), y1 - front // 2 - max(0, s(1) // 2), x1 - s(5), y1 - front // 2, 'v')   # light
    c.rect(x0 + s(4), y1 - front // 2, x0 + s(20), y1 - front // 2, 'K')                      # vent
    return c.g


def rom(n):
    c = Canvas(n)
    s = c.s
    x0, y0, x1, y1 = s(10), s(6), n - 1 - s(10), n - 1 - s(6)
    legs = max(3, (y1 - y0) // max(2, s(5)))
    step = (y1 - y0 - s(4)) / max(1, legs - 1)
    for i in range(legs):
        y = round(y0 + s(2) + i * step)
        c.rect(x0 - s(4), y, x0 - 1, y + max(0, s(2) - 1), 'p')
        c.rect(x1 + 1, y, x1 + s(4), y + max(0, s(2) - 1), 'p')
    c.box(x0, y0, x1, y1, 'k')
    c.rect(x0 + 1, y0 + 1, x1 - 1, y0 + 1, 'l')
    c.rect((x0 + x1) // 2 - s(2), y0, (x0 + x1) // 2 + s(2), y0 + s(2), 'K')   # notch
    if n >= 32:
        c.rect(x0 + s(4), (y0 + y1) // 2, x1 - s(4), (y0 + y1) // 2 + s(2), 'C')   # label
    return c.g


def xpm(name, g):
    used = sorted(set(ch for row in g for ch in row))
    out = ['/* XPM */', 'static char * %s[] = {' % name,
           '"%d %d %d 1",' % (len(g[0]), len(g), len(used))]
    out += ['"%s c %s",' % (ch, PAL[ch]) for ch in used]
    out += ['"%s"%s' % (''.join(row), ',' if i < len(g) - 1 else '};') for i, row in enumerate(g)]
    return '\n'.join(out) + '\n'


ICONS = (('AmixUAE', app), ('AmigaADF', adf), ('AmigaHDF', hdf), ('AmigaROM', rom))
SIZES = (('l', 48), ('m', 32), ('t', 16))

if __name__ == '__main__':
    here = os.path.dirname(os.path.abspath(__file__))
    os.makedirs(os.path.join(here, 'icons'), exist_ok=True)
    for name, fn in ICONS:
        for size, n in SIZES:
            open(os.path.join(here, 'icons', '%s.%s.pm' % (name, size)), 'w').write(
                xpm('%s_%s' % (name, size), fn(n)))
    if len(sys.argv) > 1:              # a sheet to look at: 4x, on CDE's grey
        from PIL import Image
        z = 4
        sheet = Image.new('RGB', ((48 + 32 + 16 + 40) * z, len(ICONS) * 56 * z), (174, 178, 195))
        for r, (name, fn) in enumerate(ICONS):
            x = 8 * z
            for size, n in SIZES:
                g = fn(n)
                for yy, row in enumerate(g):
                    for xx, ch in enumerate(row):
                        if ch != '.':
                            col = PAL[ch]
                            rgb = tuple(int(col[i:i + 2], 16) for i in (1, 3, 5))
                            sheet.paste(rgb, (x + xx * z, r * 56 * z + 4 * z + yy * z,
                                              x + (xx + 1) * z, r * 56 * z + 4 * z + (yy + 1) * z))
                x += (n + 12) * z
        sheet.save(sys.argv[1])
