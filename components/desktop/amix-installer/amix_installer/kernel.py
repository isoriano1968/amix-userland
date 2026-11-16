"""Kernels made on this computer from the user's own /stand/unix.

A 68030 machine with a graphics card (a Picasso II, also FS-UAE's) keeps its
stock AMIX kernel and gets one driver more: /dev/rtgfb (major 54).  The stock
kernel is Commodore's and not ours to ship, so the installer links the driver
into the one on the user's core disk, here, at install time; the result goes
only into the user's own out folder.

The kit (amix-gate usr/src/uts/amiga/io/rtgfb/port) holds the driver as a
68020 object, its .data pad, and the two kernel checks; the relink is
`ld -r` plus a relocation retarget in cdevsw[54], nothing byte-patched.  The
checks here are those of relink-030-rtgfb.sh, in Python: only ld comes from
the AMIX cross tools.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

KIT_FILES = ("rtgfb_030.o", "rtgfb_pad.o", "patch_rtgfb_cdevsw.py", "check_relink_relocs.py")
KIT_ENV = "AMIX_RTGFB_KIT"
# ld -r of the AMIX cross tools (developer/amix/gcc-27): Commodore's binutils,
# as the kernel's own build.  m68k-elf-ld passes every check too, but its
# output is not byte-identical and has not booted: not used until it has.
LINKERS = ("m68k-cbm-sysv4-ld",)
RTGFB_ENTRIES = ("rtgfbopen", "rtgfbclose", "rtgfbioctl", "rtgfbmmap")
BASE_NEEDS = ("autocon", "getminor", "copyin", "copyout", "bzero", "printf", "nodev", "cdevsw")
NAME = "unix.rtgfb"

SHN_UNDEF, SHN_ABS, SHN_COMMON = 0, 0xfff1, 0xfff2
STB_LOCAL = 0


class KernelError(Exception):
    pass


def find_kit():
    """The rtgfb 030 kit: $AMIX_RTGFB_KIT, an amix-gate checkout's port/kit,
    or the installer's share folder."""
    here = os.path.dirname(os.path.abspath(__file__))
    cands = [os.environ.get(KIT_ENV),
             os.path.expanduser("~/Sources/usr-amix/amix-gate/usr/src/uts/amiga/io/rtgfb/port/kit"),
             # <prefix>/lib/amix-installer/amix_installer -> <prefix>/share/...
             os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(here))), "share",
                          "amix-installer", "rtgfb")]
    for d in cands:
        if d and all(os.path.isfile(os.path.join(d, f)) for f in KIT_FILES):
            return d
    return None


def check_kit(kit):
    """The kit's files against its KIT.sha256 (make-kit-030.sh writes it)."""
    import hashlib
    sums = os.path.join(kit, "KIT.sha256")
    if not os.path.isfile(sums):
        raise KernelError("the kit has no KIT.sha256")
    want = {}
    with open(sums) as f:
        for line in f:
            w = line.split()
            if len(w) == 2:
                want[w[1]] = w[0]
    for name in KIT_FILES:
        with open(os.path.join(kit, name), "rb") as f:
            got = hashlib.sha256(f.read()).hexdigest()
        if want.get(name) != got:
            raise KernelError("kit file %s does not match KIT.sha256" % name)


# where developer/amix/gcc-27 installs the cross tools (not on the user's PATH),
# and a cross build in the home directory
LD_DIRS = ("/opt/amix-cross/bin", os.path.expanduser("~/opt/amix-cross/bin"))


def find_ld():
    for name in LINKERS:
        p = shutil.which(name) or shutil.which(name, path=os.pathsep.join(LD_DIRS))
        if p:
            return p
    return None


# -- ELF (big-endian, 32 bit, m68k) -------------------------------------------
class Elf(object):
    def __init__(self, data):
        if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 2:
            raise KernelError("not a big-endian 32-bit ELF file")
        if struct.unpack(">H", data[18:20])[0] != 4:
            raise KernelError("not an m68k ELF file")
        self.b = data
        shoff, = struct.unpack(">I", data[32:36])
        shentsize, shnum, shstrndx = struct.unpack(">HHH", data[46:52])
        self.sh = []
        for i in range(shnum):
            o = shoff + i * shentsize
            name, typ, flags, addr, off, size, link, info, align, entsize = \
                struct.unpack(">IIIIIIIIII", data[o:o + 40])
            self.sh.append(dict(name=name, type=typ, addr=addr, offset=off, size=size,
                                link=link, entsize=entsize))
        strs = self.sh[shstrndx]
        for s in self.sh:
            s["nm"] = self._str(strs, s["name"])

    def _str(self, sec, off):
        o = sec["offset"] + off
        return self.b[o:self.b.index(b"\0", o)].decode("latin-1")

    def section(self, name):
        return next((s for s in self.sh if s["nm"] == name), None)

    def symbols(self):
        """(name, shndx, bind) of every symbol of .symtab"""
        st = next((s for s in self.sh if s["type"] == 2), None)
        if st is None:
            raise KernelError("no symbol table (a stripped kernel cannot be relinked)")
        strs = self.sh[st["link"]]
        out = []
        for o in range(st["offset"] + 16, st["offset"] + st["size"], 16):
            name, value, size, info, other, shndx = struct.unpack(">IIIBBH", self.b[o:o + 16])
            out.append((self._str(strs, name), shndx, info >> 4))
        return out

    def defined(self):
        """Names of the global symbols defined here (nm's T, D, B, C)."""
        return [n for n, shndx, bind in self.symbols()
                if n and shndx != SHN_UNDEF and bind != STB_LOCAL]

    def undefined(self):
        return [n for n, shndx, bind in self.symbols() if n and shndx == SHN_UNDEF]


def check_base(data):
    """A stock 68030 kernel that can take rtgfb; else KernelError with why."""
    elf = Elf(data)
    have = set(elf.defined())
    if "cputype" in have:
        raise KernelError("this is a 68040/68060 port kernel, not the stock 68030 one")
    if "rtgfbopen" in have:
        raise KernelError("this kernel already has /dev/rtgfb")
    missing = [s for s in BASE_NEEDS if s not in have]
    if missing:
        raise KernelError("not an AMIX kernel: no %s" % ", ".join(missing))
    return elf


def check_result(data):
    elf = Elf(data)
    defs = elf.defined()
    for s in RTGFB_ENTRIES:
        if defs.count(s) != 1:
            raise KernelError("%s: %d definitions, not one" % (s, defs.count(s)))
    leak = [n for n in elf.undefined() if n not in ("edata", "end", "etext")]
    if leak:
        raise KernelError("unresolved after the relink: %s" % ", ".join(sorted(set(leak))[:8]))
    text, dat = elf.section(".text"), elf.section(".data")
    if text is None or dat is None:
        raise KernelError("no .text or .data")
    if text["offset"] + text["size"] != dat["offset"]:
        raise KernelError(".text and .data are not contiguous: AMIX's loader needs them so")
    if dat["size"] % 4:
        raise KernelError(".data is not a multiple of 4 bytes")


def relink_rtgfb(base, out, kit=None, ld=None, log=None):
    """base: the bytes of the user's /stand/unix.  Writes out (the new kernel).
    Returns its sha256."""
    import hashlib
    log = log or (lambda t: None)
    kit = kit or find_kit()
    ld = ld or find_ld()
    if not kit:
        raise KernelError("the rtgfb kit is not installed (%s)" % ", ".join(KIT_FILES))
    if not ld:
        raise KernelError("no AMIX cross linker (%s)" % " or ".join(LINKERS))
    check_kit(kit)
    check_base(base)
    log("base kernel sha256 %s" % hashlib.sha256(base).hexdigest())
    tmp = tempfile.mkdtemp(prefix="amix-kernel-")
    try:
        bp = os.path.join(tmp, "unix.base")
        op = os.path.join(tmp, NAME)
        with open(bp, "wb") as f:
            f.write(base)

        def run(cmd, what):
            r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               universal_newlines=True)
            for line in r.stdout.splitlines():
                log(line)
            if r.returncode:
                raise KernelError("%s failed (exit %d)" % (what, r.returncode))
        run([ld, "-r", "-o", op, bp, os.path.join(kit, "rtgfb_030.o"),
             os.path.join(kit, "rtgfb_pad.o")], "ld -r")
        run([sys.executable, os.path.join(kit, "patch_rtgfb_cdevsw.py"), op],
            "the cdevsw[54] retarget")
        with open(op, "rb") as f:
            data = f.read()
        check_result(data)
        run([sys.executable, os.path.join(kit, "check_relink_relocs.py"), op],
            "the loader relocation check")
        shutil.move(op, out)
        return hashlib.sha256(data).hexdigest()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
