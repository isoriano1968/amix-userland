#!/usr/bin/env python3
"""mkicons.py -- the Printers icon as CDE XPMs (48, 32, 16 pixels): a grey
printer with a sheet coming out on top and one in the tray, and a small
blue network arc (it is found on the network)."""

PAL = {'.': 'None', 'f': '#4A505C', 'g': '#9AA2B0', 'l': '#C8CED8', 'w': '#F6F6F6',
       'k': '#5A6270', 'b': '#3C7DD0', 'n': '#2FA84F'}

def icon(n):
    g = [['.'] * n for _ in range(n)]
    def box(x0, y0, x1, y1, fill, edge='f'):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                g[y][x] = edge if y in (y0, y1) or x in (x0, x1) else fill
    m = max(1, n // 16)
    # the sheet going in, on top
    box(n * 5 // 16, m, n * 11 // 16, n * 7 // 16, 'w', 'k')
    if n >= 32:
        for i, y in enumerate(range(m + n // 10, n * 6 // 16, max(2, n // 12))):
            for x in range(n * 6 // 16, n * 10 // 16 - (i % 2) * n // 16):
                g[y][x] = 'g'
    # the body
    box(m, n * 6 // 16, n - m - 1, n * 12 // 16, 'g')
    for x in range(m + 1, n - m - 1):
        g[n * 6 // 16 + 1][x] = 'l'
    # the green light
    r = max(1, n // 24)
    for y in range(n * 8 // 16, n * 8 // 16 + r + 1):
        for x in range(n - m - 2 - n // 8, n - m - 2 - n // 8 + r + 1):
            g[y][x] = 'n'
    # the printed sheet in the tray, below
    box(n * 4 // 16, n * 11 // 16, n * 12 // 16, n - m - 1, 'w', 'k')
    # a network arc, blue, top left
    if n >= 32:
        for (cx, cy, rr) in ((m + n // 8, n * 5 // 16, n // 6), (m + n // 8, n * 5 // 16, n // 10)):
            for x in range(n):
                for y in range(n):
                    d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
                    if abs(d - rr) < 0.7 and x >= cx and y <= cy and g[y][x] == '.':
                        g[y][x] = 'b'
    return g

def xpm(name, g):
    used = sorted(set(ch for row in g for ch in row))
    out = ['/* XPM */', 'static char * %s[] = {' % name,
           '"%d %d %d 1",' % (len(g[0]), len(g), len(used))]
    out += ['"%s c %s",' % (ch, PAL[ch]) for ch in used]
    out += ['"%s"%s' % (''.join(row), ',' if i < len(g) - 1 else '};') for i, row in enumerate(g)]
    return '\n'.join(out) + '\n'

for size, suffix in ((48, 'l'), (32, 'm'), (16, 't')):
    open('icons/AmixPrinters.%s.pm' % suffix, 'w').write(xpm('AmixPrinters_%s_pm' % suffix, icon(size)))
