"""Export the reviewed CPython source changes; private adapters stay separate."""
from pathlib import Path
import difflib
import sys

root = Path(sys.argv[1])
patch = []
for name in ("configure", "configure.ac", "Python/pytime.c",
             "Modules/posixmodule.c", "Modules/zlibmodule.c"):
    path = root / name
    before = path.with_name(path.name + ".amix-original").read_text().splitlines(True)
    after = path.read_text().splitlines(True)
    patch.extend(difflib.unified_diff(before, after, "a/" + name, "b/" + name))
destination = Path("ports/python/cpython-3.11-amix.patch")
destination.write_text("".join(patch))
print(destination)
