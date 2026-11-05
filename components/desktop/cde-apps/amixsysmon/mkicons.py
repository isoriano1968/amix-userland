#!/usr/bin/env python3
"""mkicons.py -- the System Monitor icon as CDE XPMs (48, 32, 16 pixels):
a screen with a dark graph, a blue CPU curve and an orange one under it,
on a stand."""
import math

PAL = {'.': 'None', 'f': '#5A6270', 'k': '#141B29', 'g': '#1E2430', 'l': '#3A4252',
       'b': '#4E9BE8', 'o': '#E8A33D', 's': '#8A93A3'}

def icon(n):
    g = [['.'] * n for _ in range(n)]
    m = max(1, n // 16)
    top, bot = m, n * 3 // 4                    # the screen
    for y in range(top, bot):
        for x in range(m, n - m):
            edge = y in (top, bot - 1) or x in (m, n - m - 1)
            g[y][x] = 'f' if edge else 'g'
    lo, hi = top + 2, bot - 3                   # inside, a grid
    for y in range(lo, hi + 1, max(2, (hi - lo) // 3)):
        for x in range(m + 2, n - m - 2):
            g[y][x] = 'l'
    def curve(f, ch):
        last = None
        for x in range(m + 2, n - m - 2):
            t = (x - m - 2) / max(1, n - 2 * m - 5)
            y = int(hi - (hi - lo) * f(t))
            y = min(hi, max(lo, y))
            if last is not None:
                for yy in range(min(last, y), max(last, y) + 1):
                    g[yy][x] = ch
            g[y][x] = ch
            last = y
    curve(lambda t: 0.25 + 0.15 * math.sin(t * 9), 'o')
    curve(lambda t: 0.55 + 0.3 * math.sin(t * 6 + 1) * (0.5 + t / 2), 'b')
    w = max(2, n // 6)                          # the stand
    for y in range(bot, min(n, bot + max(1, n // 10))):
        for x in range(n // 2 - w // 2, n // 2 + w // 2 + 1):
            g[y][x] = 's'
    for y in range(min(n - 1, bot + max(1, n // 10)), n - m):
        for x in range(n // 4, n - n // 4):
            g[y][x] = 's'
    return g

def xpm(name, g):
    used = sorted(set(ch for row in g for ch in row))
    out = ['/* XPM */', 'static char * %s[] = {' % name,
           '"%d %d %d 1",' % (len(g[0]), len(g), len(used))]
    out += ['"%s c %s",' % (ch, PAL[ch]) for ch in used]
    out += ['"%s"%s' % (''.join(row), ',' if i < len(g) - 1 else '};') for i, row in enumerate(g)]
    return '\n'.join(out) + '\n'

for size, suffix in ((48, 'l'), (32, 'm'), (16, 't')):
    open('icons/SysMon.%s.pm' % suffix, 'w').write(xpm('SysMon_%s_pm' % suffix, icon(size)))
