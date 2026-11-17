"""Kernels made on this computer from the user's own /stand/unix, before
anything goes to the Amiga.

The stock kernel is Commodore's and not ours to ship, so the installer links
the drivers for the chosen cards (DRIVERS: a graphics card, a network card)
into the one on the user's core disk, here, at install time; the result goes
only into the user's own out folder, and AMIX installs it in step 7.

Each driver comes as a kit, its own package (developer/amix/<id>-kit, or an
amix-gate checkout's io/<id>/port/kit), one per kernel line: <id> for the
stock 68030 kernel, <id>-040 (port/kit-040) for the 68040/060 port kernels;
the base kernel says which (a port kernel defines cputype):
  KIT.sha256   checksums, and the manifest: every *.o listed is linked and
               every patch_*.py (its cdevsw slot) runs, in the order listed;
  KIT.info     id=, major=, node=, line=030|040, init=, intr=, entries=, streams=;
  check_relink_relocs.py, the loader check run on the result.
A kit without KIT.info (rtgfb-kit 0.1) is taken with the facts in DRIVERS and
no boot routine.

AMIX's io_init[] = { parinit, 0 } has one slot.  The installer writes one stub,
amixdrv_init: parinit, then each chosen driver's init (graphics, then
network), assembles it with the AMIX cross as, links it with the kits, and
retargets io_init[0] once from parinit to amixdrv_init (patch_io_init.py, from
a kit or developer/amix/kernel-kit-tools).  Nothing is byte-patched; only as
and ld come from the AMIX cross tools (developer/amix/gcc-27).
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

# ld -r and as of the AMIX cross tools: Commodore's binutils, as the kernel's
# own build.  m68k-elf-ld passes every check too, but its output is not
# byte-identical and has not booted: not used until it has.
LINKERS = ("m68k-cbm-sysv4-ld",)
ASSEMBLERS = ("m68k-cbm-sysv4-as",)
# the boot hook: io_init[] (data) is retargeted from parinit (text) to the stub
BASE_HOOK = (("io_init", ".data"), ("parinit", ".text"))
BASE_NEEDS = ("autocon", "getminor", "copyin", "copyout", "bzero", "printf", "nodev", "cdevsw")
STUB = "amixdrv_init"
IO_INIT_PATCH = "patch_io_init.py"
# the level-2 interrupt chain: int2_tbl[] (data) has no spare slot either;
# its aenintr entry is retargeted to amixdrv_int2, which calls aenintr and
# then each chosen driver's level-2 routine (KIT.info intr=)
INT2_HOOK = (("int2_tbl", ".data"), ("aenintr", ".text"))
INT2_STUB = "amixdrv_int2"
INT2_PATCH = "patch_int2.py"
RELOC_CHECK = "check_relink_relocs.py"
NAME = "unix.rtgfb"


class Driver(object):
    """A kernel driver the installer links in: what it is, where its kit is,
    what the linked kernel must define, the device node AMIX gets.  A kit's
    KIT.info overrides these facts."""

    def __init__(self, id, card, kind, node, major, entries=(), lines=("030",), init=None,
                 streams=False, intr=None, gate=None, bmajor=None, cmajor=None):
        self.id, self.card, self.kind = id, card, kind
        self.node, self.major = node, major          # /dev/<node>, character major
        # a storage driver (kind "storage") adds a controller to AMIX's own
        # dd/sd driver: its disks are /dev/dsk and /dev/rdsk (block bmajor,
        # character cmajor), there is no node of its own to make
        self.bmajor, self.cmajor = bmajor, cmajor
        self.entries = tuple(entries)                # defined exactly once after the link
        self.lines = tuple(lines)                    # kernel lines the kit is built for
        self.init = init                             # its boot routine, called by the stub
        self.intr = intr                             # its level-2 interrupt routine
        self.streams = streams
        self.package = "developer/amix/%s-kit" % id
        self.gate = gate or id                       # its folder in amix-gate's io/
        self.env = "AMIX_%s_KIT" % id.upper()

    def with_info(self, info):
        """This driver with a kit's KIT.info facts (info: dict, or None:
        a kit before KIT.info, linked without a boot routine)."""
        d = Driver(self.id, self.card, self.kind, self.node, self.major, self.entries,
                   self.lines, None, self.streams, None, self.gate, self.bmajor, self.cmajor)
        if info:
            if info.get("id", d.id) != d.id:
                raise KernelError("the %s kit says it is %s" % (d.id, info["id"]))
            if info.get("node"):
                d.node = info["node"].replace("/dev/", "", 1)
            if info.get("major"):
                d.major = int(info["major"])
            if info.get("line"):
                d.lines = tuple(info["line"].replace("|", " ").split())
            d.init = info.get("init") or None
            d.intr = info.get("intr") or None
            for k in ("bmajor", "cmajor"):
                if info.get(k):
                    setattr(d, k, int(info[k]))
            if "entries" in info:
                d.entries = tuple(info["entries"].split())
            d.streams = info.get("streams") == "yes"
        return d


# the drivers by id; a card's kit appears as amix-gate builds it (rtgfb's
# first: the others are offered and planned, and the plan names a missing kit)
DRIVERS = {
    "rtgfb": Driver("rtgfb", "Picasso II", "graphics", "rtgfb", 54,
                    ("rtgfbopen", "rtgfbclose", "rtgfbioctl", "rtgfbmmap")),
    "zz9000": Driver("zz9000", "ZZ9000", "graphics", "zz9000", 49,
                     ("zz9000open", "zz9000close", "zz9000read", "zz9000write", "zz9000ioctl",
                      "zz9000mmap")),
    "va2000": Driver("va2000", "VA2000", "graphics", "va2000", 68,
                     ("va2000open", "va2000close", "va2000read", "va2000write", "va2000ioctl",
                      "va2000mmap"), lines=("030", "040"), init="va2000init"),
    "hydra": Driver("hydra", "HydraNet", "network", "hya0", 47, ("hydrainfo",), streams=True),
    # Individual Computers X-Surf 100 (AX88796B): Zorro II, or Zorro III space
    # in an A3000, which the kits map
    "xsurf100": Driver("xsurf100", "X-Surf 100", "network", "xsf0", 56,
                       ("xsurf100info", "xsurf100init", "xsurf100intr"), lines=("030", "040"),
                       init="xsurf100init", streams=True, intr="xsurf100intr"),
    # the ZZ9000's own Ethernet port, beside its graphics (polled: no init, no intr)
    "zz9000eth": Driver("zz9000eth", "ZZ9000 Ethernet", "network", "zz0", 52,
                        ("zz9000ethinfo",), lines=("030", "040"), streams=True),
    # the Z3660's Ethernet for the 68040/060 port kernel (the Z3660 kernel line
    # has it built in): not linked by the installer until it has a 040 base
    "z3660eth": Driver("z3660eth", "Z3660 Ethernet", "network", "zen0", 51,
                       ("z3660ethinfo", "z3660eth_kvmap", "z3660eth_kvunmap"), lines=("040",),
                       streams=True, gate="z3660net"),
    # the Z3660's SD card as a second controller of AMIX's dd/sd (b18/c40):
    # for the 68040/060 port kernel (the Z3660 kernel line has it built in);
    # kit patch_z3660scsi_card.py widens sd.c's controller table
    "z3660scsi": Driver("z3660scsi", "Z3660 SD card", "storage", None, None,
                        ("z3660queue", "z3660_scsicard", "z3660scsi_kvmap",
                         "z3660scsi_kvunmap"), lines=("040",), bmajor=18, cmajor=40),
}
# the stub calls graphics before network
KIND_ORDER = ("storage", "graphics", "network")


def describe(d):
    """What a driver gives AMIX, for plans and logs."""
    if d.kind == "storage":
        return "%s (a controller of AMIX's dd/sd, b%s/c%s)" % (d.card, d.bmajor, d.cmajor)
    return "/dev/%s (major %d)" % (d.node, d.major)


def kernel_name(ids):
    """unix.rtgfb, unix.rtgfb+hydra: the linked kernel's file name."""
    return "unix." + "+".join(ids)


SHN_UNDEF, SHN_ABS, SHN_COMMON = 0, 0xfff1, 0xfff2
STB_LOCAL = 0


class KernelError(Exception):
    pass


def kit_listing(kit):
    """KIT.sha256 as [(name, sha256)] in its order, or None."""
    sums = os.path.join(kit, "KIT.sha256")
    if not os.path.isfile(sums):
        return None
    out = []
    with open(sums) as f:
        for line in f:
            w = line.split()
            if len(w) == 2:
                out.append((w[1], w[0]))
    return out


def kit_info(kit):
    """KIT.info as a dict, or None for a kit without one."""
    p = os.path.join(kit, "KIT.info")
    if not os.path.isfile(p):
        return None
    out = {}
    with open(p) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                k, v = line.split("=", 1)
                out[k.strip()] = v.strip()
    return out


def _share_dir():
    # <prefix>/lib/amix-installer/amix_installer -> <prefix>/share/amix-installer
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(here))), "share",
                        "amix-installer")


LINES = ("030", "040")


def find_kit(drv="rtgfb", line="030"):
    """A driver's kit for a kernel line: $AMIX_<ID>_KIT, an amix-gate
    checkout's io/<id>/port/kit, or the installer's share folder (the kit
    package); for the 040 line $AMIX_<ID>_040_KIT, port/kit-040, <id>-040."""
    d = DRIVERS[drv] if isinstance(drv, str) else drv
    if line not in LINES:
        raise KernelError("no kernel line %s" % line)
    sfx = "" if line == "030" else "-" + line
    env = d.env if line == "030" else "AMIX_%s_%s_KIT" % (d.id.upper(), line)
    cands = [os.environ.get(env),
             os.path.expanduser("~/Sources/usr-amix/amix-gate/usr/src/uts/amiga/io/%s/port/kit%s"
                                % (d.gate, sfx)),
             os.path.join(_share_dir(), d.id + sfx)]
    for k in cands:
        if not k:
            continue
        need = [n for n, _ in (kit_listing(k) or [])]
        if need and all(os.path.isfile(os.path.join(k, f)) for f in need):
            return k
    return None


def duplicates(listing):
    """Names a KIT.sha256 lists more than once (a broken kit build)."""
    seen, dup = set(), []
    for n, _ in listing:
        if n in seen and n not in dup:
            dup.append(n)
        seen.add(n)
    return dup


def check_tools(path):
    """The kit-tools folder a shared patch script comes from: when it has a
    KIT.sha256, every file in it matches and none is listed twice."""
    import hashlib
    d = os.path.dirname(path)
    listing = kit_listing(d)
    if listing is None:
        return
    dup = duplicates(listing)
    if dup:
        raise KernelError("%s/KIT.sha256 lists %s more than once" % (d, ", ".join(dup)))
    for name, want in listing:
        p = os.path.join(d, name)
        if not os.path.isfile(p):
            raise KernelError("the kernel kit tools in %s lack %s" % (d, name))
        with open(p, "rb") as f:
            if hashlib.sha256(f.read()).hexdigest() != want:
                raise KernelError("kit tool %s does not match KIT.sha256" % name)


def check_kit(kit, drv="rtgfb"):
    """The kit's files against its KIT.sha256; returns the driver with the
    kit's KIT.info facts."""
    import hashlib
    d = DRIVERS[drv] if isinstance(drv, str) else drv
    listing = kit_listing(kit)
    if listing is None:
        raise KernelError("the %s kit has no KIT.sha256" % d.id)
    dup = duplicates(listing)
    if dup:
        raise KernelError("the %s kit's KIT.sha256 lists %s more than once"
                          % (d.id, ", ".join(dup)))
    want = dict(listing)
    gone = [n for n in want if not os.path.isfile(os.path.join(kit, n))]
    if gone:
        raise KernelError("the %s kit in %s lacks %s" % (d.id, kit, ", ".join(gone)))
    for name in want:
        with open(os.path.join(kit, name), "rb") as f:
            got = hashlib.sha256(f.read()).hexdigest()
        if want[name] != got:
            raise KernelError("kit file %s does not match KIT.sha256" % name)
    if not any(n.endswith(".o") for n in want):
        raise KernelError("the %s kit has no driver object" % d.id)
    if os.path.isfile(os.path.join(kit, "KIT.info")) and "KIT.info" not in want:
        raise KernelError("the %s kit's KIT.info is not in its KIT.sha256" % d.id)
    return d.with_info(kit_info(kit))


def find_int2_patch(kits):
    """patch_int2.py: developer/amix/kernel-kit-tools, else any kit's."""
    for k in [os.environ.get("AMIX_KIT_TOOLS"), os.path.join(_share_dir(), "kit-tools")] + \
            list(kits):
        if k and os.path.isfile(os.path.join(k, INT2_PATCH)):
            return os.path.join(k, INT2_PATCH)
    return None


def find_io_init_patch(kits):
    """patch_io_init.py: developer/amix/kernel-kit-tools, else any kit's."""
    for k in [os.environ.get("AMIX_KIT_TOOLS"), os.path.join(_share_dir(), "kit-tools")] + \
            list(kits):
        if k and os.path.isfile(os.path.join(k, IO_INIT_PATCH)):
            return os.path.join(k, IO_INIT_PATCH)
    return None


# where developer/amix/gcc-27 installs the cross tools (not on the user's PATH),
# and a cross build in the home directory
LD_DIRS = ("/opt/amix-cross/bin", os.path.expanduser("~/opt/amix-cross/bin"))


def _find_tool(names):
    for name in names:
        p = shutil.which(name) or shutil.which(name, path=os.pathsep.join(LD_DIRS))
        if p:
            return p
    return None


def find_ld():
    return _find_tool(LINKERS)


def find_as():
    return _find_tool(ASSEMBLERS)


def int2_source(intrs):
    """amixdrv_int2: aenintr first (what int2_tbl called), then each chosen
    driver's level-2 routine, each with p2int's argument (sp@(4)) passed on;
    as amix-gate's amixdrv_int2.s.in."""
    lines = ["# amixdrv_int2 -- written by the AMIX Installer: int2_tbl's aenintr entry",
             "# calls it; aenintr first, then each chosen driver's level-2 routine",
             "\t.text", "\t.globl\t%s" % INT2_STUB, "%s:" % INT2_STUB]
    for r in ["aenintr"] + list(intrs):
        lines += ["\tmovel\t%sp@(4),%sp@-", "\tjsr\t%s" % r, "\taddql\t&4,%sp"]
    lines += ["\trts", "\t.align\t4", ""]
    return "\n".join(lines)


def stub_source(inits):
    """amixdrv_init: parinit first (what io_init[0] called), then each driver's
    boot routine, in the order given."""
    lines = ["# amixdrv_init -- written by the AMIX Installer: io_init[0] calls it",
             "# instead of parinit; parinit first, then each chosen driver's init",
             "\t.text", "\t.globl\t%s" % STUB, "%s:" % STUB, "\tjsr\tparinit"]
    lines += ["\tjsr\t%s" % i for i in inits]
    # .text stays a multiple of 4 bytes, so the kernel's .data follows it
    # directly (one init: 14 bytes without the pad)
    lines += ["\trts", "\t.align\t4", ""]
    return "\n".join(lines)


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

    def defined_in(self, name):
        """The section names a global symbol `name` is defined in."""
        return [self.sh[shndx]["nm"] for n, shndx, bind in self.symbols()
                if n == name and bind != STB_LOCAL and 0 < shndx < len(self.sh)]

    def undefined(self):
        return [n for n, shndx, bind in self.symbols() if n and shndx == SHN_UNDEF]


def _drivers(ids):
    return [DRIVERS[i] if isinstance(i, str) else i for i in ids]


LINE_NAME = {"030": "stock 68030", "040": "68040/060 port"}


def base_line(data):
    """The kernel line of an AMIX kernel: 030 (stock), 040 (a port kernel:
    it defines cputype)."""
    return "040" if "cputype" in Elf(data).defined() else "030"


def check_base(data, ids=("rtgfb",)):
    """An AMIX kernel whose line the drivers are built for, without them yet;
    else KernelError."""
    elf = Elf(data)
    have = set(elf.defined())
    line = "040" if "cputype" in have else "030"
    drvs = _drivers(ids)
    for d in drvs:
        if line not in d.lines:
            raise KernelError("this is the %s kernel; the %s driver is built for the %s kernel"
                              % (LINE_NAME[line], d.card,
                                 " and ".join(LINE_NAME[x] for x in d.lines)))
        dup = [e for e in d.entries if e in have]
        if dup:
            raise KernelError("this kernel already has %s (/dev/%s)" % (", ".join(dup), d.node))
    if STUB in have:
        raise KernelError("this kernel already has %s: it was linked before" % STUB)
    if INT2_STUB in have:
        raise KernelError("this kernel already has %s: it was linked before" % INT2_STUB)
    missing = [s for s in BASE_NEEDS if s not in have]
    if missing:
        raise KernelError("not an AMIX kernel: no %s" % ", ".join(missing))
    if [d for d in drvs if d.intr]:
        for sym, sec in INT2_HOOK:
            if elf.defined_in(sym) != [sec]:
                raise KernelError("no %s in %s: the drivers' interrupt routines have nowhere "
                                  "to go" % (sym, sec))
    if [d for d in drvs if d.init]:
        for sym, sec in BASE_HOOK:
            if elf.defined_in(sym) != [sec]:
                raise KernelError("no %s in %s: the drivers' boot routines have nowhere to go"
                                  % (sym, sec))
    return elf


def check_result(data, ids=("rtgfb",)):
    elf = Elf(data)
    defs = elf.defined()
    drvs = _drivers(ids)
    need = [s for d in drvs for s in d.entries + ((d.init,) if d.init else ()) +
            ((d.intr,) if d.intr else ())]
    if [d for d in drvs if d.init]:
        need.append(STUB)
    if [d for d in drvs if d.intr]:
        need.append(INT2_STUB)
    for s in need:
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


def relink(base, out, ids, kits=None, ld=None, log=None, asm=None):
    """base: the bytes of an AMIX kernel (the user's /stand/unix).  Links
    the drivers `ids` into it, each from its kit for the base's line, and
    writes out (the new kernel).  kits: {id: kit dir} (found when left
    out).  Returns the new kernel's sha256."""
    import hashlib
    log = log or (lambda t: None)
    kits = dict(kits or {})
    line = base_line(base)
    drvs = []
    for d in _drivers(ids):
        if line not in d.lines and not kits.get(d.id):
            raise KernelError("this is the %s kernel; the %s driver is built for the %s kernel "
                              "only" % (LINE_NAME[line], d.card,
                                        " and ".join(LINE_NAME[x] for x in d.lines)))
        kits[d.id] = kits.get(d.id) or find_kit(d, line)
        if not kits[d.id]:
            raise KernelError("the %s kit for the %s kernel is not installed (package %s)"
                              % (d.id, LINE_NAME[line], d.package))
        d = check_kit(kits[d.id], d)
        if line not in d.lines:
            raise KernelError("the %s kit in %s is built for the %s kernel, not the %s one"
                              % (d.card, kits[d.id], " and ".join(LINE_NAME[x] for x in d.lines),
                                 LINE_NAME[line]))
        drvs.append(d)
    order = sorted(drvs, key=lambda d: KIND_ORDER.index(d.kind) if d.kind in KIND_ORDER else 9)
    inits = [d.init for d in order if d.init]
    intrs = [d.intr for d in order if d.intr]
    ld = ld or find_ld()
    if not ld:
        raise KernelError("no AMIX cross linker (%s)" % " or ".join(LINKERS))
    if inits or intrs:
        asm = asm or find_as()
        if not asm:
            raise KernelError("no AMIX cross assembler (%s)" % " or ".join(ASSEMBLERS))
    if inits:
        io_patch = find_io_init_patch([kits[d.id] for d in drvs])
        if not io_patch:
            raise KernelError("no %s (package developer/amix/kernel-kit-tools)" % IO_INIT_PATCH)
        check_tools(io_patch)
    if intrs:
        int2_patch = find_int2_patch([kits[d.id] for d in drvs])
        if not int2_patch:
            raise KernelError("no %s (package developer/amix/kernel-kit-tools)" % INT2_PATCH)
        check_tools(int2_patch)
    check_base(base, drvs)
    log("base kernel sha256 %s" % hashlib.sha256(base).hexdigest())
    tmp = tempfile.mkdtemp(prefix="amix-kernel-")
    try:
        bp = os.path.join(tmp, "unix.base")
        op = os.path.join(tmp, kernel_name([d.id for d in drvs]))
        with open(bp, "wb") as f:
            f.write(base)

        def run(cmd, what):
            r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               universal_newlines=True)
            for line in r.stdout.splitlines():
                log(line)
            if r.returncode:
                raise KernelError("%s failed (exit %d)" % (what, r.returncode))
        listing = dict((d.id, [n for n, _ in kit_listing(kits[d.id])]) for d in drvs)
        objs = [os.path.join(kits[d.id], n) for d in drvs for n in listing[d.id]
                if n.endswith(".o")]
        if inits:
            src, obj = os.path.join(tmp, STUB + ".s"), os.path.join(tmp, STUB + ".o")
            with open(src, "w") as f:
                f.write(stub_source(inits))
            log("%s: parinit, %s" % (STUB, ", ".join(inits)))
            run([asm, "-o", obj, src], "assembling %s" % STUB)
            objs.append(obj)
        if intrs:
            src, obj = os.path.join(tmp, INT2_STUB + ".s"), os.path.join(tmp, INT2_STUB + ".o")
            with open(src, "w") as f:
                f.write(int2_source(intrs))
            log("%s: aenintr, %s" % (INT2_STUB, ", ".join(intrs)))
            run([asm, "-o", obj, src], "assembling %s" % INT2_STUB)
            objs.append(obj)
        run([ld, "-r", "-o", op, bp] + objs, "ld -r")
        # each kit's own retargets (its cdevsw slot), in its KIT.sha256 order;
        # the shared hooks once each: the level-2 chain, then the boot chain
        for d in drvs:
            for n in listing[d.id]:
                if n.startswith("patch_") and n.endswith(".py") and \
                        n not in (IO_INIT_PATCH, INT2_PATCH):
                    run([sys.executable, os.path.join(kits[d.id], n), op],
                        "%s (%s)" % (n, d.card))
        if intrs:
            run([sys.executable, int2_patch, op, INT2_STUB],
                "the level-2 interrupt chain (%s)" % INT2_STUB)
        if inits:
            run([sys.executable, io_patch, op, STUB], "the io_init boot hook (%s)" % STUB)
        with open(op, "rb") as f:
            data = f.read()
        check_result(data, drvs)
        check = next((os.path.join(kits[d.id], RELOC_CHECK) for d in drvs
                      if RELOC_CHECK in listing[d.id]), None)
        if check is None:
            raise KernelError("no kit has the loader check (%s)" % RELOC_CHECK)
        run([sys.executable, check, op], "the loader relocation check")
        shutil.move(op, out)
        return hashlib.sha256(data).hexdigest()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def relink_rtgfb(base, out, kit=None, ld=None, log=None):
    """The Picasso II alone (relink with one driver)."""
    return relink(base, out, ["rtgfb"], {"rtgfb": kit} if kit else None, ld, log)
