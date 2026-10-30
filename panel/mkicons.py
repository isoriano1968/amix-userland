#!/usr/bin/env python3
"""mkicons.py LOGO.pm -- CDE icons (48, 32, 16 pixels) as XPM: AmixCast, a screen
with a play arrow and radio waves; AmixPanel, the AMIX logo made small."""
import sys, re
from PIL import Image, ImageDraw

CHARS = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789'


def xpm(name, im, colours=24):
    q = im.convert('RGB').quantize(colors=colours, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal, alpha, w, h = q.getpalette(), im.convert('RGBA').getchannel('A'), im.size[0], im.size[1]
    px, a = list(q.getdata()), list(alpha.getdata())
    used = sorted(set(p for p, t in zip(px, a) if t >= 128))
    code = {c: CHARS[i] for i, c in enumerate(used)}
    out = ['/* XPM */', 'static char * %s[] = {' % name, '"%d %d %d 1",' % (w, h, len(used) + 1), '". c None",']
    out += ['"%s c #%02X%02X%02X",' % ((code[c],) + tuple(pal[3 * c:3 * c + 3])) for c in used]
    for y in range(h):
        row = ''.join(code[px[y * w + x]] if a[y * w + x] >= 128 else '.' for x in range(w))
        out.append('"%s"%s' % (row, ',' if y < h - 1 else '};'))
    return '\n'.join(out) + '\n'


def cast(n):
    s = n * 8                           # drawn large, reduced for clean edges
    im = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    u = s / 48.0
    d.rounded_rectangle((2 * u, 12 * u, 36 * u, 40 * u), 3 * u, fill=(24, 32, 52, 255), outline=(200, 208, 224, 255), width=int(2 * u))
    d.polygon([(13 * u, 18 * u), (13 * u, 34 * u), (27 * u, 26 * u)], fill=(57, 211, 83, 255))
    d.rectangle((14 * u, 40 * u, 24 * u, 43 * u), fill=(120, 130, 150, 255))
    d.rectangle((9 * u, 43 * u, 29 * u, 45 * u), fill=(200, 208, 224, 255))
    for r in (11, 19, 27):              # the stream arriving at the corner
        d.arc((35 * u - r * u, 11 * u - r * u, 35 * u + r * u, 11 * u + r * u), -80, 5, fill=(255, 170, 40, 255), width=int(3 * u))
    d.ellipse((32 * u, 8 * u, 38 * u, 14 * u), fill=(255, 170, 40, 255))
    return im.resize((n, n), Image.LANCZOS)


def read_xpm(path):
    t = open(path).read()
    rows = re.findall(r'"([^"]*)"', t)
    w, h, nc, _ = map(int, rows[0].split())
    col = {}
    for r in rows[1:1 + nc]:
        m = re.match(r'(.)\s+c\s+#(\w{6})', r)
        col[m.group(1)] = tuple(int(m.group(2)[i:i + 2], 16) for i in (0, 2, 4))
    im = Image.new('RGB', (w, h))
    im.putdata([col[c] for r in rows[1 + nc:1 + nc + h] for c in r])
    return im


logo = read_xpm(sys.argv[1])
for size, suffix in ((48, 'l'), (32, 'm'), (16, 't')):
    open('../amixcast/icons/AmixCast.%s.pm' % suffix, 'w').write(xpm('AmixCast_%s_pm' % suffix, cast(size)))
    open('AmixPanel.%s.pm' % suffix, 'w').write(xpm('AmixPanel_%s_pm' % suffix, logo.resize((size, size), Image.LANCZOS)))
