"""The host tools the tests need, on a machine without them.

pvmk:

REAL: a pvmk the installer finds by itself (the package, AMIX_PVMK_DIR or
a pvmk build).  Without one, use() points AMIX_PVMK_DIR at a stand-in with
the files find_pvmk() looks for, so the tests that only check what a plan
contains run everywhere; tests that run a plan's pvmk disk step need the
real one and are skipped without it (needs_real).
Kernel tools: KERNEL and needs_kernel, below."""
import os
import stat
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from amix_installer import kernel, plan  # noqa: E402

REAL = plan.find_pvmk() is not None
needs_real = unittest.skipUnless(REAL, "no pvmk here (developer/amix/pvmk)")
# the kernel step of a plan: the AMIX cross linker (developer/amix/gcc-27)
# and the driver kits (developer/amix/*-kit, kernel-kit-tools); a plan for
# cards needs them, so its tests run where they are installed
KERNEL = bool(kernel.find_ld() and kernel.find_kit("rtgfb", "030"))
needs_kernel = unittest.skipUnless(KERNEL, "no AMIX kernel tools here (developer/amix/gcc-27 and the kits)")
_dir = None


def use():
    """AMIX_PVMK_DIR -> a stand-in, unless a real pvmk is found."""
    global _dir
    if REAL or _dir:
        return
    _dir = tempfile.mkdtemp(prefix="pvmk-stub-")
    share, lib = os.path.join(_dir, "share", "pvmk"), os.path.join(_dir, "lib", "pvmk")
    os.makedirs(os.path.join(share, "conf"))
    os.makedirs(lib)
    for f in ("bootblock.bin", "pvmk.bin", "conf/README.disk"):
        open(os.path.join(share, f), "wb").close()
    with open(os.path.join(share, "conf", "pvmk.conf"), "w") as f:
        f.write("# stand-in\n")
    for t in ("mkadf", "mkhdf"):
        p = os.path.join(lib, t)
        with open(p, "w") as f:
            f.write("#!/bin/sh\necho 'pvmk stand-in: not a real %s' >&2\nexit 1\n" % t)
        os.chmod(p, os.stat(p).st_mode | stat.S_IXUSR)
    os.environ["AMIX_PVMK_DIR"] = _dir
