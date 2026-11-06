#!/usr/bin/env python3
"""mkicons.py SRC DIR -- NetSurf's CDE icons (48, 32, 16 pixels) as XPM,
NetSurf.l.pm, .m.pm and .t.pm, scaled from the GTK frontend's icon."""
import sys
from PIL import Image, ImageColor

CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#$%&*+@"


def color(v):
    """#rrggbb, an X11 grayN (N percent), or a name PIL knows"""
    if v.lower().startswith(("gray", "grey")) and v[4:].isdigit():
        g = round(int(v[4:]) * 255 / 100)
        return (g, g, g)
    return ImageColor.getrgb(v)[:3]


def readxpm(src):
    """an XPM with any chars per pixel (PIL reads only one)"""
    import re
    q = re.findall(r'"([^"]*)"', open(src, encoding="latin-1").read())
    w, h, nc, cpp = map(int, q[0].split()[:4])
    col = {}
    for line in q[1:1 + nc]:
        key, rest = line[:cpp], line[cpp:].split()
        v = rest[rest.index("c") + 1] if "c" in rest else rest[-1]
        col[key] = (0, 0, 0, 0) if v.lower() == "none" else \
            color(v) + (255,)
    im = Image.new("RGBA", (w, h))
    for y, row in enumerate(q[1 + nc:1 + nc + h]):
        for x in range(w):
            im.putpixel((x, y), col[row[x * cpp:(x + 1) * cpp]])
    return im


def square(src):
    im = readxpm(src)
    n = max(im.size)
    sq = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    sq.paste(im, ((n - im.width) // 2, (n - im.height) // 2))
    return sq


def icon(im, n):
    im = im.resize((n, n), Image.LANCZOS)
    alpha = im.getchannel("A")
    pal = im.convert("RGB").quantize(colors=48)
    rgb = pal.getpalette()
    rows, used = [], {}
    for y in range(n):
        row = ""
        for x in range(n):
            if alpha.getpixel((x, y)) < 128:
                row += " "
                used[" "] = "None"
                continue
            i = pal.getpixel((x, y))
            c = CHARS[i]
            used[c] = "#%02x%02x%02x" % tuple(rgb[3 * i:3 * i + 3])
            row += c
        rows.append(row)
    return rows, used


def write(path, name, rows, used):
    with open(path, "w") as f:
        f.write('/* XPM */\nstatic char * %s[] = {\n"%d %d %d 1",\n' % (name, len(rows[0]), len(rows), len(used)))
        for c in sorted(used):
            f.write('"%s c %s",\n' % (c, used[c]))
        f.write(",\n".join('"%s"' % r for r in rows) + "};\n")


src = square(sys.argv[1])
for n, s in ((48, "l"), (32, "m"), (16, "t")):
    rows, used = icon(src, n)
    write("%s/NetSurf.%s.pm" % (sys.argv[2], s), "NetSurf_" + s, rows, used)
