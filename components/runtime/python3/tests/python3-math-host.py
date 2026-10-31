"""Compare private math adapters with host libm; not target acceptance."""
from pathlib import Path
import ctypes
import math
import random
import struct
import subprocess

names = ("expm1", "log1p", "log2", "exp2", "round", "nextafter")
destination = Path("build/python-math-host.so").resolve()
destination.parent.mkdir(exist_ok=True)
wrapper = destination.with_suffix(".c")
# Include glibc's declarations before renaming our definitions: renaming in
# compiler -D options also breaks glibc's vector-math declaration machinery.
wrapper.write_text("#include <math.h>\n" +
                   "".join("#define " + name + " amix_" + name + "\n" for name in names) +
                   '#include "' + Path("ports/python/amix-math.c").resolve().as_posix() + '"\n')
subprocess.run(["gcc", "-shared", "-fPIC", "-O2", "-fno-strict-aliasing", "-fno-builtin",
                str(wrapper), "-lm", "-o", str(destination)], check=True)
library = ctypes.CDLL(str(destination))
for name in names:
    function = getattr(library, "amix_" + name)
    function.argtypes = [ctypes.c_double] * (2 if name == "nextafter" else 1)
    function.restype = ctypes.c_double

def bits(value):
    return struct.pack("!d", value)

rng = random.Random(0x68060)
for name, domain in (("expm1", (-700, 700)), ("log1p", (-0.999, 100)),
                     ("log2", (1e-100, 1e100)), ("exp2", (-1075, 1023))):
    function = getattr(library, "amix_" + name)
    reference = getattr(math, name)
    maximum = 0.0
    for value in [0.0, -0.0, 1e-20, -1e-20] + [rng.uniform(*domain) for _ in range(4000)]:
        if name == "log2" and value <= 0:
            continue
        expected = reference(value)
        actual = function(value)
        if expected == 0.0:
            assert bits(actual) == bits(expected), (name, value, actual, expected)
        else:
            ulps = abs(actual - expected) / math.ulp(expected)
            maximum = max(maximum, ulps)
            assert ulps <= 8, (name, value, actual, expected, ulps)
    print(name, "4000 random samples plus edge cases; max error in ulps:", maximum)

values = [0.0, -0.0, 1.0, -1.0, math.inf, -math.inf, math.ulp(0.0),
          -math.ulp(0.0), float.fromhex("0x1.fffffffffffffp1023")]
for x in values:
    for y in values:
        actual = library.amix_nextafter(x, y)
        expected = math.nextafter(x, y)
        assert bits(actual) == bits(expected), (x, y, actual, expected)
for _ in range(10000):
    x, y = rng.uniform(-1e200, 1e200), rng.uniform(-1e200, 1e200)
    assert bits(library.amix_nextafter(x, y)) == bits(math.nextafter(x, y))
for x, expected in ((-2.5, -3), (-1.5, -2), (-0.5, -1), (-0.0, -0.0),
                    (0.0, 0.0), (0.5, 1), (1.5, 2), (2.5, 3)):
    assert bits(library.amix_round(x)) == bits(float(expected))
print("PYTHON_MATH_HOST_COMPARISON_PASS; target tests still required")
