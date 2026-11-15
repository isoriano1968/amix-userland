#!/usr/bin/env python3
"""mkicons.py LOGO OUTDIR -- the app icons from the AMIX logo (pycairo).

org.amix.AMIX:      the logo on a GNOME-style plate (rounded square, beige
                    face, darker base edge)
org.amix.Installer: the same with a blue wrench badge at the lower right
Writes OUTDIR/hicolor/<n>x<n>/apps/<id>.png.
"""
import math
import os
import sys

import cairo

SIZES = (16, 24, 32, 48, 64, 128, 256, 512)
WRENCH = "M14.7 6.3a4 4 0 0 0-5.4 5.4L3 18l3 3 6.3-6.3a4 4 0 0 0 5.4-5.4l-2.6 2.6-2.4-2.4z"


def rounded(cr, x, y, w, h, r):
    cr.new_sub_path()
    cr.arc(x + w - r, y + r, r, -math.pi / 2, 0)
    cr.arc(x + w - r, y + h - r, r, 0, math.pi / 2)
    cr.arc(x + r, y + h - r, r, math.pi / 2, math.pi)
    cr.arc(x + r, y + r, r, math.pi, 3 * math.pi / 2)
    cr.close_path()


def svg_arc(cr, x1, y1, rx, ry, phi, fa, fs, x2, y2):
    """SVG elliptical arc (phi = 0, rx = ry here) to cairo."""
    r = rx
    dx, dy = (x1 - x2) / 2.0, (y1 - y2) / 2.0
    d2 = dx * dx + dy * dy
    r = max(r, math.sqrt(d2))
    f = math.sqrt(max(0.0, (r * r - d2) / d2))
    if fa == fs:
        f = -f
    cx = (x1 + x2) / 2.0 + f * dy
    cy = (y1 + y2) / 2.0 - f * dx
    a1 = math.atan2(y1 - cy, x1 - cx)
    a2 = math.atan2(y2 - cy, x2 - cx)
    if fs:
        cr.arc(cx, cy, r, a1, a2)
    else:
        cr.arc_negative(cx, cy, r, a1, a2)


def wrench(cr):
    import re
    toks = re.findall(r"[a-zA-Z]|-?\d*\.?\d+", WRENCH)
    i = 0
    x = y = 0.0
    cmd = None
    while i < len(toks):
        if toks[i].isalpha():
            cmd = toks[i]
            i += 1
            if cmd in "zZ":
                cr.close_path()
                continue
        n = lambda k: float(toks[i + k])  # noqa: E731
        if cmd == "M":
            x, y = n(0), n(1)
            cr.move_to(x, y)
            i += 2
            cmd = "L"
        elif cmd in "lL":
            nx, ny = n(0), n(1)
            if cmd == "l":
                nx, ny = x + nx, y + ny
            x, y = nx, ny
            cr.line_to(x, y)
            i += 2
        elif cmd == "a":
            nx, ny = x + n(5), y + n(6)
            svg_arc(cr, x, y, n(0), n(1), n(2), int(n(3)), int(n(4)), nx, ny)
            x, y = nx, ny
            i += 7
        else:
            raise ValueError(cmd)


def icon(logo, n, badge):
    s = cairo.ImageSurface(cairo.FORMAT_ARGB32, n, n)
    cr = cairo.Context(s)
    m = n * 0.06                       # margin, as GNOME app icons have
    w = n - 2 * m
    rad = w * 0.22
    # base edge
    rounded(cr, m, m, w, w, rad)
    cr.set_source_rgb(0x8f / 255.0, 0x84 / 255.0, 0x6c / 255.0)
    cr.fill()
    # face with the logo, 94 % high
    cr.save()
    rounded(cr, m, m, w, w * 0.94, rad)
    cr.clip()
    cr.set_source_rgb(0xc2 / 255.0, 0xb6 / 255.0, 0x9c / 255.0)
    cr.paint()
    sc = w / float(logo.get_width())
    cr.translate(m, m)
    cr.scale(sc, sc)
    cr.set_source_surface(logo, 0, 0)
    cr.get_source().set_filter(cairo.FILTER_BEST)
    cr.paint()
    cr.restore()
    if badge:
        b = n * 0.42
        cx = n - m - b * 0.36
        cy = n - m - b * 0.36
        ring = max(1.0, n * 0.025)
        cr.arc(cx, cy, b / 2 + ring, 0, 2 * math.pi)
        cr.set_source_rgb(1, 1, 1)
        cr.fill()
        cr.arc(cx, cy, b / 2, 0, 2 * math.pi)
        cr.set_source_rgb(0x35 / 255.0, 0x84 / 255.0, 0xe4 / 255.0)
        cr.fill()
        g = b * 0.62
        cr.save()
        cr.translate(cx - g / 2, cy - g / 2)
        cr.scale(g / 24.0, g / 24.0)
        wrench(cr)
        cr.restore()
        cr.set_source_rgb(1, 1, 1)
        cr.set_line_width(max(1.0, 2.6 * g / 24.0))
        cr.set_line_cap(cairo.LINE_CAP_ROUND)
        cr.set_line_join(cairo.LINE_JOIN_ROUND)
        cr.stroke()
    return s


def main():
    logo = cairo.ImageSurface.create_from_png(sys.argv[1])
    out = sys.argv[2]
    for n in SIZES:
        d = os.path.join(out, "hicolor", "%dx%d" % (n, n), "apps")
        os.makedirs(d, exist_ok=True)
        icon(logo, n, False).write_to_png(os.path.join(d, "org.amix.AMIX.png"))
        icon(logo, n, True).write_to_png(os.path.join(d, "org.amix.Installer.png"))


if __name__ == "__main__":
    main()
