"""python3-speed.py -- how fast Python runs here: a few loops in the shape of
an interactive program (PyDPainter's redraw is Python glue around C calls),
each timed by itself.  Prints ms per item and a total; compare machines by
running the same file on both.  Standard library only.

    python3 python3-speed.py [scale]     (scale 1 = about 1 s on a 2020 PC)
"""
import sys
import time

SCALE = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0


def timed(name, fn, n):
    n = max(1, int(n * SCALE))
    t = time.perf_counter()
    fn(n)
    dt = (time.perf_counter() - t) * 1000.0
    print("%-28s %6d x  %10.1f ms  %9.3f ms each" % (name, n, dt, dt / n))
    return dt


def calls(n):
    # method calls and attribute access: event dispatch, tool objects
    class Tool:
        def __init__(self):
            self.x = 0
            self.y = 0

        def move(self, dx, dy):
            self.x += dx
            self.y += dy
            return self.x

    t = Tool()
    for i in range(n * 1000):
        t.move(1, -1)


def ints(n):
    # integer arithmetic in a loop: coordinates, clipping, line stepping
    for _ in range(n):
        x, y, err = 0, 0, 0
        for i in range(1000):
            err += 37
            if err > 100:
                err -= 100
                y += 1
            x += 1


def floats(n):
    # float maths: scaling, perspective, colour conversion
    import math
    s = 0.0
    for i in range(n * 1000):
        s += math.sin(i * 0.001) * 0.5 + math.sqrt(i + 1.0)


def containers(n):
    # dicts, lists, tuples, string formatting: menus, palettes, requesters
    for _ in range(n):
        pal = [(i, (i * 3) & 255, (i * 7) & 255) for i in range(256)]
        d = {("c%d" % i): rgb for i, rgb in enumerate(pal)}
        s = sorted(d.items(), key=lambda kv: kv[1][2])
        "".join("%02x%02x%02x" % rgb for _, rgb in s[:64])


def pixels_python(n):
    # a 320x16 strip expanded through a palette in pure Python (what is slow
    # if the program does it itself instead of in a C module)
    pal = [(i * 0x010101) & 0xFFFFFF for i in range(256)]
    src = bytes(range(256)) * 20
    for _ in range(n):
        [pal[c] for c in src]


def pixels_bytes(n):
    # the same through C-level byte operations: bytes.translate, slicing
    table = bytes((i * 3) & 255 for i in range(256))
    src = bytes(range(256)) * 250          # 64000 bytes = 320x200 at 8 bits
    for _ in range(n):
        b = src.translate(table)
        bytearray(b)[::2]


def imports(n):
    # module import from disk (bytecode cache): program start
    import importlib
    import importlib.util
    for _ in range(n):
        for name in ("json", "textwrap", "string", "fractions"):
            sys.modules.pop(name, None)
            importlib.import_module(name)


print("Python %s on %s" % (sys.version.split()[0], sys.platform))
total = 0.0
total += timed("method calls (1000)", calls, 200)
total += timed("integer loop (1000)", ints, 300)
total += timed("float maths (1000)", floats, 100)
total += timed("dict/list/format", containers, 100)
total += timed("palette 5 KB, Python", pixels_python, 200)
total += timed("palette 64 KB, bytes ops", pixels_bytes, 200)
total += timed("import 4 modules", imports, 20)
print("total %.0f ms" % total)
