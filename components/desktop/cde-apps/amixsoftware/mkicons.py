#!/usr/bin/env python3
"""mkicons.py -- the AMIX Software Center icon as CDE XPMs (48, 32, 16
pixels): a cardboard package seen from the front and above, with tape over
its lid, and a blue arrow coming down into it (software arriving)."""

PAL = {'.': 'None', 'e': '#5B3A17', 't': '#E2B36E', 'f': '#C88E45', 's': '#A8712F',
       'p': '#F0D9A8', 'b': '#2F6FC0', 'B': '#1C4C8C', 'w': '#FFFFFF'}


def icon(n):
    g = [['.'] * n for _ in range(n)]

    def put(x, y, c):
        if 0 <= x < n and 0 <= y < n:
            g[y][x] = c

    m = max(1, n // 16)
    top_y0 = n * 7 // 16            # the lid's back edge
    top_y1 = n * 10 // 16           # the lid's front edge = the front's top
    front_x0, front_x1 = m + n // 16, n * 11 // 16
    side_x1 = n - m - 1
    bottom = n - m - 1
    depth = side_x1 - front_x1      # how far the box goes back to the right
    # the front face
    for y in range(top_y1, bottom + 1):
        for x in range(front_x0, front_x1 + 1):
            put(x, y, 'e' if y in (top_y1, bottom) or x in (front_x0, front_x1) else 'f')
    # the right side, slanting back
    for x in range(front_x1, side_x1 + 1):
        k = x - front_x1
        y0 = top_y1 - k * (top_y1 - top_y0) // max(1, depth)
        y1 = bottom - k * (top_y1 - top_y0) // max(1, depth)
        for y in range(y0, y1 + 1):
            put(x, y, 'e' if y in (y0, y1) or x == side_x1 else 's')
    # the lid
    for y in range(top_y0, top_y1 + 1):
        k = (top_y1 - y) * depth // max(1, top_y1 - top_y0)
        for x in range(front_x0 + k, front_x1 + k + 1):
            edge = y in (top_y0, top_y1) or x in (front_x0 + k, front_x1 + k)
            put(x, y, 'e' if edge else 't')
    # the tape across the lid and down the front
    mid = (front_x0 + front_x1) // 2
    tw = max(1, n // 16)
    for y in range(top_y0 + 1, top_y1):
        k = (top_y1 - y) * depth // max(1, top_y1 - top_y0)
        for x in range(mid + k - tw, mid + k + tw):
            put(x, y, 'p')
    for y in range(top_y1 + 1, top_y1 + max(2, n // 6)):
        for x in range(mid - tw, mid + tw):
            put(x, y, 'p')
    # the arrow, coming down onto the lid
    ax = mid + depth // 2
    shaft = max(1, n // 12)
    head = max(2, n // 6)
    tip = top_y0 + 1
    for y in range(m, tip - head + 1):
        for x in range(ax - shaft, ax + shaft + 1):
            put(x, y, 'B' if x in (ax - shaft, ax + shaft) else 'b')
    for i in range(head + 1):
        y = tip - head + i
        w = head - i
        for x in range(ax - w, ax + w + 1):
            put(x, y, 'B' if x in (ax - w, ax + w) or i == 0 else 'b')
    return g


def xpm(name, g):
    used = sorted(set(ch for row in g for ch in row))
    out = ['/* XPM */', 'static char * %s[] = {' % name,
           '"%d %d %d 1",' % (len(g[0]), len(g), len(used))]
    out += ['"%s c %s",' % (ch, PAL[ch]) for ch in used]
    out += ['"%s"%s' % (''.join(row), ',' if i < len(g) - 1 else '};') for i, row in enumerate(g)]
    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    import os
    os.makedirs('icons', exist_ok=True)
    for size, suffix in ((48, 'l'), (32, 'm'), (16, 't')):
        open('icons/AmixSoftware.%s.pm' % suffix, 'w').write(xpm('AmixSoftware_%s_pm' % suffix, icon(size)))
