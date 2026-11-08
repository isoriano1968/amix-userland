#!/usr/bin/env python3
"""mkicons.py -- amixq3's icon as CDE XPMs, large/medium/tiny (48, 32, 16):
a gun-metal crosshair over a dark red arena with a torch glow (our own
drawing, no game logo).

  python3 mkicons.py           the XPMs into icons/
"""
import math
import os

PAL = {'.': 'None', 'K': '#141414', 'r': '#4A1410', 'R': '#7A2418', 'o': '#C8641E',
       'y': '#F0C040', 'g': '#9AA0A8', 'G': '#D8DCE2'}


def icon(n):
    g = [['.'] * n for _ in range(n)]
    c = (n - 1) / 2.0
    r = n / 2.0 - 0.5
    for y in range(n):
        for x in range(n):
            d = math.hypot(x - c, y - c)
            if d <= r:
                g[y][x] = 'K' if d > r - max(1, n // 24) else ('R' if y > c + d * 0.2 else 'r')
    # a torch glow low left
    gx, gy = n * 0.3, n * 0.68
    for y in range(n):
        for x in range(n):
            d = math.hypot(x - gx, y - gy)
            if g[y][x] in 'rR' and d < n * 0.08:
                g[y][x] = 'y'
            elif g[y][x] in 'rR' and d < n * 0.14:
                g[y][x] = 'o'
    # the crosshair: a ring and four ticks
    rr, w = n * 0.28, max(1, n // 24)
    for y in range(n):
        for x in range(n):
            d = math.hypot(x - c, y - c)
            if abs(d - rr) < w * 0.75 and g[y][x] != '.':
                g[y][x] = 'G'
    t0, t1 = int(c - n * 0.42), int(c - n * 0.14)
    for k in range(t0, t1 + 1):
        for j in range(-(w // 2), w - w // 2):
            ic = int(round(c)) + j
            for (x, y) in ((k, ic), (n - 1 - k, ic), (ic, k), (ic, n - 1 - k)):
                if 0 <= x < n and 0 <= y < n and g[y][x] != '.':
                    g[y][x] = 'g'
    g[int(round(c))][int(round(c))] = 'G'
    return g


def xpm(name, g):
    used = sorted(set(ch for row in g for ch in row))
    out = ['/* XPM */', 'static char * %s[] = {' % name, '"%d %d %d 1",' % (len(g[0]), len(g), len(used))]
    out += ['"%s c %s",' % (ch, PAL[ch]) for ch in used]
    out += ['"%s"%s' % (''.join(row), ',' if i < len(g) - 1 else '};') for i, row in enumerate(g)]
    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    here = os.path.dirname(os.path.abspath(__file__))
    os.makedirs(os.path.join(here, 'icons'), exist_ok=True)
    for size, n in (('l', 48), ('m', 32), ('t', 16)):
        open(os.path.join(here, 'icons', 'AmixQ3.%s.pm' % size), 'w').write(xpm('AmixQ3_%s' % size, icon(n)))
