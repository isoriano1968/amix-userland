"""Export reviewable patches against the pinned GNU Pth source/configure output."""
from pathlib import Path
import difflib
import sys

root = Path(sys.argv[1])
output = Path("patches")
output.mkdir(exist_ok=True)
for source, name in (
    ("pth_mctx.c", "0001-amix-global-errno.patch"),
    ("pthread.h", "0002-amix-public-timespec.patch"),
    ("pthread.c", "0003-amix-yielding-once.patch"),
):
    before = (root / (source + ".amix-original")).read_text().splitlines(True)
    after = (root / source).read_text().splitlines(True)
    patch = "".join(difflib.unified_diff(before, after, "a/" + source, "b/" + source))
    if not patch:
        raise SystemExit("No port changes found for " + source)
    destination = output / name
    destination.write_text(patch)
    print(destination)
