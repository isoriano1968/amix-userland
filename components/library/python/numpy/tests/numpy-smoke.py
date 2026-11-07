"""numpy-smoke.py [--pygame]: NumPy as PyDPainter uses it, with results that
must match between the PC and AMIX.  Each check prints one line, name and a
short digest of its result; the last line is the digest of all of them.
Run it with the same NumPy version on the PC (the reference) and on the box,
and compare the last lines (or the whole output with diff).

--pygame adds pygame.surfarray and pygame.pixelcopy on a small 8-bit and a
32-bit surface (SDL's dummy video driver, no window).

Deterministic: no clocks, no unseeded randomness, no memory addresses.
"""
import hashlib, sys

import numpy as np

results = []


def norm(v):
    """the same on big- and little-endian machines: arrays as little-endian
    bytes, numpy scalars as Python numbers, containers element by element"""
    if isinstance(v, np.ndarray):
        le = v.dtype.newbyteorder("<") if v.dtype.byteorder != "|" else v.dtype
        return (le.str, v.shape, np.ascontiguousarray(v, dtype=le).tobytes())
    if isinstance(v, np.generic):
        return norm(np.asarray(v))
    if isinstance(v, (tuple, list)):
        return tuple(norm(x) for x in v)
    return v


def check(name, value):
    data = repr(norm(value)).encode()
    h = hashlib.sha256(data).hexdigest()[:16]
    results.append(h)
    print("%-28s %s" % (name, h))


check("version", np.__version__)
a = np.arange(48, dtype=np.uint8).reshape(6, 8)
check("arange-reshape", a)
check("sum-axis", a.sum(axis=0))
check("where", np.where(a % 3 == 0, a, 255).astype(np.uint8))
check("equal-not_equal", np.equal(a, 7) | np.not_equal(a, 9))
check("logical_not-invert", np.logical_not(a > 20) ^ np.invert(a.astype(bool)))
check("unique-sort-argsort", (np.unique(a % 5), np.sort(a[::-1, ::-1], axis=None), np.argsort(a[2][::-1])))
check("amax-amin-count", (int(np.amax(a)), int(np.amin(a)), int(np.count_nonzero(a & 1))))
check("append-insert", np.insert(np.append(a[0], [99, 100]), 3, 77))
check("tile-indices", (np.tile(np.array([1, 2], np.int16), 3), np.indices((2, 3))))
check("packbits", (np.packbits(a[:2] & 1), np.unpackbits(np.array([5, 250], np.uint8))))
check("frombuffer-copyto", np.frombuffer(bytes(range(16)), dtype=np.uint16))
b = np.zeros((4, 4), np.int32)
np.copyto(b, 3)
check("zeros-copyto", b)
check("nonzero-argwhere", (np.nonzero(a == 13), np.argwhere(a > 45)))
check("setdiff-diff", (np.setdiff1d(np.arange(10), [2, 3, 5]), np.diff(np.array([1, 4, 9, 16]))))

# PyDPainter's transforms: np.matrix, matmul, linalg.inv (lapack-lite)
m = np.matrix([[1.0, 0.5, 0.0], [0.0, 1.0, 0.25], [0.0, 0.0, 1.0]])
r = np.matrix([[np.cos(0.3), -np.sin(0.3), 0.0], [np.sin(0.3), np.cos(0.3), 0.0], [0.0, 0.0, 1.0]])
t = np.matmul(m, r)
check("matrix-matmul", np.round(t, 12))
check("linalg-inv", np.round(np.linalg.inv(t), 12))
check("inv-identity", np.allclose(np.linalg.inv(t) @ t, np.eye(3)))
check("solve-det", (np.round(np.linalg.solve(t, [1.0, 2.0, 3.0]), 12), round(float(np.linalg.det(t)), 12)))

# float maths through libm99, lib060 and AMIX's libm
x = np.linspace(-3.0, 3.0, 13)
check("sin-cos-tan", np.round([np.sin(x), np.cos(x), np.tan(x / 4)], 12))
check("exp-log-sqrt", np.round([np.exp(x), np.log1p(np.abs(x)), np.sqrt(np.abs(x)), np.cbrt(x)], 12))
check("arctan2-hypot-power", np.round([np.arctan2(x, 1.5), np.hypot(x, 2.0), np.power(np.abs(x), 1.7)], 12))
check("float32", np.round(np.sin(x.astype(np.float32)) * np.float32(2.5), 5))
check("rint-floor-trunc", (np.rint(x * 1.25), np.floor(x * 1.25), np.trunc(x * 1.25), np.modf(np.float32(2.75))))
check("longdouble", (np.finfo(np.longdouble).nmant, str(np.longdouble(1) / 3)[:18]))
check("complex", np.round(np.exp(1j * x[:5]) * np.sqrt(-1 + 0j), 12))
check("fft", np.round(np.abs(np.fft.fft(np.arange(8.0))), 9))
check("int64", (np.int64(2) ** 40 // 3, np.uint64(10 ** 18) % np.uint64(97)))
check("strtoll", (np.array(["-9223372036854775808", "12345678901234"]).astype(np.int64)))
check("repr-format", (repr(np.float64(0.1)), str(np.float32(1 / 3)), np.array2string(x[:4], precision=3)))

rng = np.random.RandomState(1234)
check("random-randint", rng.randint(0, 256, size=12))
g = np.random.default_rng(42)
check("random-generator", np.round(g.random(6), 12))

if "--pygame" in sys.argv:
    import os
    os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
    os.environ["PYGAME_HIDE_SUPPORT_PROMPT"] = "1"
    import pygame
    pygame.display.init()
    pygame.display.set_mode((16, 16))
    s8 = pygame.Surface((8, 6), depth=8)
    s8.set_palette([(i, 255 - i, i // 2) for i in range(256)])
    px = pygame.surfarray.pixels2d(s8)
    px[:, :] = a.T
    del px
    check("surf8-pixels2d", pygame.surfarray.array2d(s8))
    check("surf8-array3d", pygame.surfarray.array3d(s8))
    s32 = pygame.Surface((8, 6), pygame.SRCALPHA, 32)
    s32.fill((10, 20, 30, 128))
    al = pygame.surfarray.pixels_alpha(s32)
    al[2:5, 1:3] = 200
    del al
    check("surf32-alpha", (pygame.surfarray.array_alpha(s32), pygame.surfarray.array3d(s32)))
    ms = pygame.surfarray.make_surface(np.dstack([a.T, a.T // 2, 255 - a.T]).astype(np.uint8))
    check("make_surface", pygame.surfarray.array3d(ms))
    p3 = pygame.surfarray.pixels3d(ms)
    p3[0, 0] = (1, 2, 3)
    del p3
    check("pixels3d", pygame.surfarray.array3d(ms)[0, :2])

print("%-28s %s" % ("ALL", hashlib.sha256("".join(results).encode()).hexdigest()[:16]))
