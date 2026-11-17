"""From a Config to an ordered plan of actions, and running it safely.

Rules every action follows (doc/plans/AMIX-INSTALLER-APP-PLAN.md):
- the source image is opened read-only and never written;
- every output is written as NAME.part, checked (sha256 read back), and only
  then renamed to NAME; an existing file is never overwritten;
- the .part files of a run are listed in a journal in the output folder
  before the first byte is written, so an interrupted run is cleaned up;
- free space on the host is checked before anything is written.

An action that this version cannot do yet is in the plan with the reason;
the plan says whether it is complete.
"""
import hashlib
import json
import os
import shutil
import struct
import subprocess
import tempfile
import time

from . import __version__, fsuae, hdf, model, safety

CHUNK = 4 << 20
JOURNAL = ".amix-installer-journal"

# pvmk: the boot loader on its own partition (include/pvmk/layout.h)
PVMK_HOLE_ROOM = (320 - 16 - 32) * 1024


class Cancelled(Exception):
    pass


class Action(object):
    """One step of a plan.  run(ctx) does it; ctx.progress(fraction, text)."""

    def __init__(self, id, title, detail, size=0, outputs=(), missing=None):
        self.id = id
        self.title = title
        self.detail = detail
        self.size = size                # bytes it writes on the host
        self.outputs = list(outputs)    # file names it creates in the output folder
        self.missing = missing          # why this version cannot do it, or None

    @property
    def available(self):
        return self.missing is None

    def run(self, ctx):
        raise NotImplementedError


class Plan(object):
    def __init__(self, cfg, paths):
        self.cfg = cfg
        self.paths = dict(paths)
        self.actions = []
        self.problems = []              # stop the whole plan

    @property
    def complete(self):
        return not self.problems and all(a.available for a in self.actions)

    def runnable(self):
        return [a for a in self.actions if a.available]

    def write_bytes(self):
        return sum(a.size for a in self.runnable())

    def text(self):
        out = []
        for p in self.problems:
            out.append("! %s" % p)
        for n, a in enumerate(self.actions, 1):
            mark = " " if a.available else "-"
            out.append("%s %2d. %s" % (mark, n, a.title))
            out.append("       %s" % a.detail)
            if a.missing:
                out.append("       not in this version: %s" % a.missing)
        return "\n".join(out)


# --- the actions -----------------------------------------------------------

class Context(object):
    def __init__(self, out_dir, progress=None, log=None, cancel=None):
        self.out_dir = out_dir
        self._progress = progress or (lambda f, t: None)
        self._log = log or (lambda t: None)
        self._cancel = cancel or (lambda: False)
        self.results = {}               # name -> sha256

    def progress(self, fraction, text=""):
        if self._cancel():
            raise Cancelled()
        self._progress(fraction, text)

    def log(self, text):
        self._log(text)

    def part(self, name):
        return os.path.join(self.out_dir, name + ".part")

    def final(self, name):
        return os.path.join(self.out_dir, name)


def sha256_file(path, ctx=None, what=""):
    h = hashlib.sha256()
    size = os.path.getsize(path)
    done = 0
    with open(path, "rb") as f:
        while True:
            b = f.read(CHUNK)
            if not b:
                break
            h.update(b)
            done += len(b)
            if ctx:
                ctx.progress(done / float(size or 1), what)
    return h.hexdigest()


def finish(ctx, name, expect_sha=None):
    """Read the .part back, compare, fsync, rename to its name."""
    part, final = ctx.part(name), ctx.final(name)
    got = sha256_file(part, ctx, "checking %s" % name)
    if expect_sha and got != expect_sha:
        raise IOError("%s: read back %s, expected %s" % (name, got[:16], expect_sha[:16]))
    if os.path.exists(final):
        raise safety.Refused("%s appeared while writing; not overwritten" % final)
    os.rename(part, final)
    ctx.results[name] = got
    ctx.log("%s  %s" % (got, name))
    return got


class CopyCore(Action):
    """The core HDF, byte for byte; the source is only read."""

    def __init__(self, src, name):
        size = os.path.getsize(src)
        super(CopyCore, self).__init__(
            "copy-core", "Copy the core system",
            "%s → %s (%d MB); the original is never written"
            % (os.path.basename(src), name, size >> 20), size, [name])
        self.src = src
        self.name = name

    def run(self, ctx):
        part = ctx.part(self.name)
        h = hashlib.sha256()
        size = os.path.getsize(self.src)
        done = 0
        fd = os.open(self.src, os.O_RDONLY)
        try:
            with open(part, "xb") as out:
                while True:
                    b = os.read(fd, CHUNK)
                    if not b:
                        break
                    h.update(b)
                    out.write(b)
                    done += len(b)
                    ctx.progress(done / float(size), "copying %d of %d MB" % (done >> 20, size >> 20))
                out.flush()
                os.fsync(out.fileno())
        finally:
            os.close(fd)
        if done != size:
            raise IOError("%s changed size while it was copied" % self.src)
        ctx.log("source %s  %s" % (h.hexdigest(), os.path.basename(self.src)))
        finish(ctx, self.name, h.hexdigest())


def check_patch_disk(path):
    """None when path is the AmigaUNIX 2.1 patch disk image (Patch Disks 1
    and 2: a /sbin/sh loader, then the cpio of /var/patch), else why not."""
    if not path:
        return "choose your patch disk image on the Base system page"
    try:
        with open(path, "rb") as f:
            head = f.read(1024)
    except OSError as e:
        return "%s: %s" % (path, e.strerror or e)
    if not head.startswith(b"#!/sbin/sh") or b"Patch Disks 1 and 2" not in head or \
            b"/var/patch/apply" not in head:
        return "%s is not the AmigaUNIX 2.1 patch disk (Patch Disks 1 and 2)" % path
    return None


PVMK_FILES = ("bootblock", "image", "mkadf", "mkhdf")


def pvmk_files(d):
    """pvmk in d, as {bootblock, image, mkadf, mkhdf, conf, readme}, or None.
    d is a pvmk build directory (build/: bootblock.bin, pvmk.bin, host/mkadf,
    host/mkhdf; conf/ beside it) or an installed prefix (the package
    developer/amix/pvmk: share/pvmk/{bootblock.bin,pvmk.bin,conf/},
    lib/pvmk/{mkadf,mkhdf})."""
    if not d:
        return None
    share, lib = os.path.join(d, "share", "pvmk"), os.path.join(d, "lib", "pvmk")
    for f in ({"bootblock": os.path.join(d, "bootblock.bin"), "image": os.path.join(d, "pvmk.bin"),
               "mkadf": os.path.join(d, "host", "mkadf"), "mkhdf": os.path.join(d, "host", "mkhdf"),
               "conf": os.path.join(d, "..", "conf", "pvmk.conf"),
               "readme": os.path.join(d, "..", "conf", "README.disk")},
              {"bootblock": os.path.join(share, "bootblock.bin"),
               "image": os.path.join(share, "pvmk.bin"),
               "mkadf": os.path.join(lib, "mkadf"), "mkhdf": os.path.join(lib, "mkhdf"),
               "conf": os.path.join(share, "conf", "pvmk.conf"),
               "readme": os.path.join(share, "conf", "README.disk")}):
        if all(os.path.isfile(f[k]) for k in PVMK_FILES) and \
                all(os.access(f[k], os.X_OK) for k in ("mkadf", "mkhdf")):
            return f
    return None


def find_pvmk():
    """pvmk for the boot disk (see pvmk_files): $AMIX_PVMK_DIR, a build in
    ~/Sources/usr-amix/pvmk/build, or the package developer/amix/pvmk under
    the installer's own prefix or /usr."""
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    prefix = os.path.dirname(os.path.dirname(here))      # <prefix>/lib/amix-installer
    env = os.environ.get("AMIX_PVMK_DIR")
    for d in (env, os.path.expanduser("~/Sources/usr-amix/pvmk/build"), prefix, "/usr"):
        f = pvmk_files(d)
        if f:
            return f
    return None


def pvmk_conf(cfg, template=None):
    """pvmk.conf for the chosen machine: AMIX from UNIX_Root:/stand/unix."""
    lines = []
    if template and os.path.exists(template):
        with open(template) as f:
            lines = f.read().splitlines()
    want = {"z3reloc": "yes" if cfg.sel["accel"] == "z3660" else "no", "default": "AMIX",
            "keymap": {"us": "us", "de": "de", "depc": "de-pc"}[cfg.sel["keymap"]]}
    out, entry_done, seen = [], False, set()
    for ln in lines:
        key = ln.split("=", 1)[0].strip()
        if key == "entry":
            if not entry_done:
                out.append("entry=AMIX,UNIX_Root:/stand/unix,native")
                entry_done = True
            if not ln.startswith("entry=AmigaOS"):
                continue
        if key in want and not ln.lstrip().startswith("#"):
            ln = "%s=%s" % (key, want[key])
            seen.add(key)
        out.append(ln)
    if not entry_done:
        out.append("entry=AMIX,UNIX_Root:/stand/unix,native")
    if kernel_drivers(cfg.sel):
        # the relinked kernel replaces /stand/unix; the stock one stays a choice
        i = out.index("entry=AMIX,UNIX_Root:/stand/unix,native")
        out.insert(i + 1, "entry=AMIX stock,UNIX_Root:/stand/unix.stock,native")
    for k, v in want.items():
        if k not in seen:
            out.append("%s=%s" % (k, v))
    return "\n".join(out) + "\n"


class PvmkDisk(Action):
    """pvmk's boot volume with our pvmk.conf, as an RDB disk of its own
    (pvmk.hdf: on the Z3660 SD card, SCSI unit 5)."""

    def __init__(self, cfg, pv, name="pvmk.hdf"):
        missing = None if pv else ("pvmk not found: install developer/amix/pvmk, "
                                   "or set AMIX_PVMK_DIR to a pvmk build")
        super(PvmkDisk, self).__init__(
            "pvmk", "Write the pvmk boot disk",
            "%s: boot menu entry AMIX = UNIX_Root:/stand/unix, keymap %s%s" %
            (name, cfg.sel["keymap"], ", z3reloc" if cfg.sel["accel"] == "z3660" else ""),
            1 << 20, [name], missing)
        self.cfg = cfg
        self.pv = pv
        self.name = name

    def run(self, ctx):
        pvm = self.pv
        tmp = tempfile.mkdtemp(prefix="amix-installer-")
        try:
            conf = os.path.join(tmp, "pvmk.conf")
            with open(conf, "w") as f:
                f.write(pvmk_conf(self.cfg, pvm["conf"]))
            adf = os.path.join(tmp, "pvmk.adf")
            args = [pvm["mkadf"], "-q", "-o", adf, "-b", pvm["bootblock"], "-i", pvm["image"],
                    "-n", "PVMK", "-f", "pvmk.conf=" + conf]
            if os.path.exists(pvm["readme"]):
                args += ["-f", "README=" + pvm["readme"]]
            ctx.progress(0.1, "making the boot volume")
            run(args, ctx)
            run([pvm["mkadf"], "--verify", adf, "-q"], ctx)
            with open(adf, "rb") as f:
                bb = f.read(1024)
            pv = bb[hdf.PVBB_OFFSET:hdf.PVBB_OFFSET + 64]
            if struct.unpack_from(">I", pv, 0)[0] != hdf.PVBB_MAGIC:
                raise IOError("pvmk.adf has no PVBB parameter area")
            alloc = struct.unpack_from(">I", pv, 20)[0]
            if alloc > PVMK_HOLE_ROOM:
                raise IOError("pvmk needs %d bytes, the hole has room for %d: not written"
                              % (alloc, PVMK_HOLE_ROOM))
            ctx.log("pvmk fits the hole: %d of %d bytes" % (alloc, PVMK_HOLE_ROOM))
            ctx.progress(0.5, "making the disk")
            part = ctx.part(self.name)
            run([pvm["mkhdf"], "-o", part,
                 "-p", "PVMK,image=%s,pri=4,bootblocks=2,pvbb" % adf], ctx)
            with hdf.Image(part) as im:
                kinds = [p.kind for p in im.read_rdb()]
            if kinds != ["pvmk"]:
                raise IOError("the new pvmk disk does not read back as a pvmk partition")
            finish(ctx, self.name)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


def run(args, ctx):
    p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       universal_newlines=True)
    for ln in p.stdout.splitlines():
        ctx.log("  " + ln)
    if p.returncode:
        raise IOError("%s failed (%d)" % (os.path.basename(args[0]), p.returncode))


class Checksums(Action):
    def __init__(self, cfg, readme_text=None):
        names = ["SHA256SUMS", "amix-installer.json"] + (["README-amix-installer.txt"]
                                                         if readme_text else [])
        super(Checksums, self).__init__(
            "sums", "Write checksums, a README and the record of this run",
            "%s next to the disks" % ", ".join(names), 4096, names)
        self.cfg = cfg
        self.readme = readme_text

    def run(self, ctx):
        from . import profile
        sums = "".join("%s  %s\n" % (h, n) for n, h in sorted(ctx.results.items()))
        rec = {"installer": __version__, "time": time.strftime("%Y-%m-%dT%H:%M:%S"),
               "profile": profile.dump(self.cfg), "files": ctx.results}
        files = [("SHA256SUMS", sums),
                 ("amix-installer.json", json.dumps(rec, indent=2, sort_keys=True) + "\n")]
        if self.readme:
            files.append(("README-amix-installer.txt", self.readme))
        for name, text in files:
            with open(ctx.part(name), "x") as f:
                f.write(text)
                f.flush()
                os.fsync(f.fileno())
            if os.path.exists(ctx.final(name)):
                raise safety.Refused("%s exists; not overwritten" % ctx.final(name))
            os.rename(ctx.part(name), ctx.final(name))
        ctx.progress(1.0, "done")


class FragCheck(Action):
    """Z3660 firmware beta 21 misdirects HDFs with 32 or more fragments."""

    LIMIT = 32

    def __init__(self, names):
        tool = shutil.which("filefrag")
        super(FragCheck, self).__init__(
            "frag", "Check the HDFs on the SD card for fragments",
            "fewer than %d pieces each (Z3660 firmware)" % self.LIMIT, 1, [],
            None if tool else "no filefrag on this host; copy to an empty card")
        self.names = names

    def run(self, ctx):
        bad, unknown = [], []
        for n in self.names:
            p = ctx.final(n)
            if not os.path.exists(p):
                continue
            r = subprocess.run(["filefrag", p], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               universal_newlines=True)
            words = r.stdout.split()
            ext = int(words[-3]) if r.returncode == 0 and len(words) >= 3 and \
                words[-3].isdigit() else None
            ctx.log("%s: %s" % (n, r.stdout.strip()))
            if ext is None:
                unknown.append(n)
            elif ext >= self.LIMIT:
                bad.append(n)
        if bad:
            raise IOError("%s: %d or more fragments; the Z3660 firmware would misdirect them. "
                          "Write them to an empty card." % (", ".join(bad), self.LIMIT))
        if unknown:
            raise IOError("cannot count the fragments of %s on this filesystem; write the SD card "
                          "folder where filefrag works (FAT or exFAT on Linux), or choose "
                          "\u201cHDF files only\u201d and copy them to an empty card"
                          % ", ".join(unknown))


class Later(Action):
    """In the plan, with the reason this version cannot do it."""

    def __init__(self, id, title, detail, why, size=0):
        super(Later, self).__init__(id, title, detail, size, [], why)


# AMIX writes its own file system: what changes UFS runs on AMIX, in FS-UAE
# with an emulated A2065 or on an Amiga with an A2065 (version 0.4)
NO_UFS = "AMIX does this itself in FS-UAE, over the network (0.4)"
NO_PKG = "AMIX does this with its package client (0.4)"
NO_NET = "the network install comes in version 0.4"


def default_repo():
    """The package repository: $AMIX_REPO, else what "make repo" publishes
    into the installer's build folder, else ~/.local/share/amix-installer/repo."""
    env = os.environ.get("AMIX_REPO")
    if env:
        return env
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    for d in (os.path.join(here, "build", "repo"),
              os.path.expanduser("~/.local/share/amix-installer/repo")):
        if os.path.isfile(os.path.join(d, "pkg5.repository")):
            return d
    return None


class NetInstall(Action):
    """AMIX installs itself over its A2065: on the network install page,
    after the disks (or at once, for a running system)."""

    def __init__(self, detail, repo, missing=None):
        super(NetInstall, self).__init__(
            "install", "Install over the network", detail, 1, [], missing)
        self.repo = repo

    def run(self, ctx):
        ctx.log("the network install continues on the next page")
        ctx.progress(1.0, "")


# SCSI IDs, the same in FS-UAE and on a ZuluSCSI/BlueSCSI: the stock kernel
# roots on ID 6; pvmk's boot disk 5; the data disk 1 (the host adapter is 7)
SCSI_ID = {"core": 6, "pvmk": 5, "data": 1}
FSUAE_CONFIG = "amix-install.fs-uae"
# the installer's graphics card -> FS-UAE's graphics_card
FSUAE_CARD = {"picasso2": "picasso-ii"}
README = "README-amix-installer.txt"


def disk_names(sel, stem):
    """File names of the disks for the target."""
    if sel["target"] == "card":
        return dict((k, "HD%d0_512.hda" % v) for k, v in SCSI_ID.items())
    return {"core": "%s-core.hdf" % stem, "pvmk": "pvmk.hdf", "data": "%s-data.hdf" % stem}


class DataDisk(Action):
    """An empty data disk: RDB from the core disk, one UNIX partition; AMIX
    makes the file system (mkfs -F ufs) when it installs."""

    def __init__(self, src, name, size):
        super(DataDisk, self).__init__(
            "data-disk", "Create the data disk",
            "%s, %d MB: an empty UNIX_Data partition; AMIX makes its file system"
            % (name, size >> 20), 1 << 20, [name])
        self.src = src
        self.name = name
        self.bytes = size

    def run(self, ctx):
        from . import rdb
        ctx.progress(0.2, "partition table")
        rdb.make_data_disk(self.src, ctx.part(self.name), self.bytes)
        with hdf.Image(ctx.part(self.name)) as im:
            parts = im.read_rdb()
        if [p.kind for p in parts] != ["amix-root"]:
            raise IOError("the new data disk does not read back as one UNIX partition")
        finish(ctx, self.name)


# the installer's choices -> the kernel drivers linked in (kernel.DRIVERS)
GFX_DRIVER = {"picasso2": "rtgfb", "zz9000": "zz9000", "va2000": "va2000"}
NET_DRIVER = {"nethydra": "hydra"}


def kernel_drivers(sel):
    """The driver ids the chosen cards need in the kernel, graphics first."""
    out = []
    if sel.get("gfx") in GFX_DRIVER:
        out.append(GFX_DRIVER[sel["gfx"]])
    if sel.get("net") in NET_DRIVER:
        out.append(NET_DRIVER[sel["net"]])
    return out


class DriverKernel(Action):
    """The user's own stock kernel with the chosen cards' drivers linked in
    (kernel.py), here, before anything goes to the Amiga.  AMIX installs it
    over the network, last; the stock kernel stays as /stand/unix.stock."""

    def __init__(self, src, ids, kern="stock", patch_missing=False):
        from . import kernel
        drvs = [kernel.DRIVERS[i] for i in ids]
        kits = dict((d.id, kernel.find_kit(d)) for d in drvs)
        ld = kernel.find_ld()
        why = None
        cards = " and ".join(d.card for d in drvs)
        if kern != "stock":
            # the 040 kits exist (va2000-040), but the port kernel reaches the
            # Amiga as a package: there is no 040 base kernel on this computer
            why = "%s: the installer links drivers into your stock 68030 kernel only; " \
                  "linking into the %s kernel needs that kernel on this computer, " \
                  "which the installer does not have yet" % (cards, kern)
        elif [d for d in drvs if "030" not in d.lines]:
            d = [d for d in drvs if "030" not in d.lines][0]
            why = "the %s driver exists for the %s kernel only" % (d.card, "/".join(d.lines))
        elif patch_missing:
            # the patch disk rebuilds /stand/unix: link into the patched one
            why = "the core disk lacks Patch Disk 1-2a: apply it, then run the installer again"
        elif [d for d in drvs if not kits[d.id]]:
            gone = [d for d in drvs if not kits[d.id]]
            why = "no kernel kit for %s yet (package %s)" % (
                " and ".join(d.card for d in gone), ", ".join(d.package for d in gone))
        elif not ld:
            why = "the AMIX cross linker %s is not installed (developer/amix/gcc-27)" \
                % kernel.LINKERS[0]
        elif [k for k in kits.values() if (kernel.kit_info(k) or {}).get("init")]:
            # boot routines: the installer's stub, assembled and hooked once
            if not kernel.find_as():
                why = "the AMIX cross assembler %s is not installed (developer/amix/gcc-27)" \
                    % kernel.ASSEMBLERS[0]
            elif not kernel.find_io_init_patch(list(kits.values())):
                why = "%s is not installed (developer/amix/kernel-kit-tools)" \
                    % kernel.IO_INIT_PATCH
        if not why and [k for k in kits.values() if (kernel.kit_info(k) or {}).get("intr")]:
            # level-2 interrupt routines: the installer's chain, hooked once
            if not kernel.find_as():
                why = "the AMIX cross assembler %s is not installed (developer/amix/gcc-27)" \
                    % kernel.ASSEMBLERS[0]
            elif not kernel.find_int2_patch(list(kits.values())):
                why = "%s is not installed (developer/amix/kernel-kit-tools)" \
                    % kernel.INT2_PATCH
        self.name = kernel.kernel_name(ids)
        nodes = ", ".join("/dev/%s (major %d)" % (d.node, d.major) for d in drvs)
        super(DriverKernel, self).__init__(
            "kernel", "Link the %s driver%s into your kernel" % (cards, "s" if len(drvs) > 1 else ""),
            "%s: your /stand/unix plus %s; the stock kernel stays as /stand/unix.stock"
            % (self.name, nodes), 2 << 20, [self.name], why)
        self.src = src
        self.ids = ids
        self.kits, self.ld = kits, ld

    def run(self, ctx):
        from . import kernel
        ctx.progress(0.1, "reading /stand/unix")
        with hdf.Image(self.src) as im:
            parts = im.read_rdb()
            root = next((p for p in parts if p.kind == "amix-root" and
                         p.name.lower() == "unix_root"), None) or \
                next(p for p in parts if p.kind == "amix-root")
            base = hdf.Ufs(im, root).read_file("/stand/unix", limit=16 << 20)
        if base is None:
            raise IOError("the core disk has no /stand/unix")
        ctx.progress(0.4, "linking")
        try:
            kernel.relink(base, ctx.part(self.name), self.ids, self.kits, self.ld, ctx.log)
        except kernel.KernelError as e:
            raise IOError("the kernel with %s: %s" % (", ".join(self.ids), e))
        finish(ctx, self.name)


class FsUaeConfig(Action):
    """An FS-UAE A3000 with an A2065 and the new disks: the machine the
    network install fills (0.4).  The Kickstart is the user's own file."""

    def __init__(self, names, kickstart, out, gfx=None):
        disks = [n for n in (names.get("pvmk"), names.get("core"), names.get("data")) if n]
        missing = None if kickstart else "choose your A3000 Kickstart ROM on the Base system page"
        super(FsUaeConfig, self).__init__(
            "fs-uae", "Write the FS-UAE configuration",
            "%s: A3000, A2065 network, %s" % (FSUAE_CONFIG, ", ".join(disks)), 4096,
            [FSUAE_CONFIG], missing)
        self.names = names
        self.kickstart = kickstart
        self.out = out
        self.gfx = gfx

    def text(self):
        out = ["# AMIX installation machine, written by amix-installer %s" % __version__,
               "# An A3000 with an A2065: the installer fills the disks over its network.",
               "[fs-uae]", "amiga_model = A3000", "chip_memory = 2048",
               "motherboard_ram = 16384", "kickstart_file = %s" % self.kickstart,
               "network_card = a2065"]
        if self.gfx in FSUAE_CARD:
            out.append("graphics_card = %s" % FSUAE_CARD[self.gfx])
        n = 0
        for role in ("pvmk", "core", "data"):
            name = self.names.get(role)
            if not name:
                continue
            out += ["hard_drive_%d = %s" % (n, os.path.join(self.out, name)),
                    "hard_drive_%d_controller = scsi%d" % (n, SCSI_ID[role]),
                    "hard_drive_%d_type = rdb" % n]
            n += 1
        return "\n".join(out) + "\n"

    def run(self, ctx):
        with open(ctx.part(FSUAE_CONFIG), "x") as f:
            f.write(self.text())
        finish(ctx, FSUAE_CONFIG)


class FsUaeRegister(Action):
    """The configuration in FS-UAE's own list, as "AMIX Installer": the last
    step, a new file only (an existing one is never replaced)."""

    def __init__(self, fs, text_of):
        super(FsUaeRegister, self).__init__(
            "fs-uae-list", "Add “%s” to FS-UAE" % fsuae.CONFIG_NAME,
            fs.config_path(), 4096, [])
        self.fs = fs
        self.text_of = text_of

    def run(self, ctx):
        dst = self.fs.config_path()
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if os.path.exists(dst):
            raise safety.Refused("%s exists; not replaced" % dst)
        with open(dst + ".part", "x") as f:
            f.write(self.text_of())
            f.flush()
            os.fsync(f.fileno())
        os.rename(dst + ".part", dst)
        ctx.log("%s written" % dst)
        ctx.progress(1.0, "done")


def readme(cfg, names):
    sel = cfg.sel
    lines = ["Disks written by the AMIX Installer %s." % __version__, ""]
    for role, what in (("core", "the core system (root, swap, boot)"),
                       ("pvmk", "pvmk, the boot loader (boots first)"),
                       ("data", hdf.MOVABLE_TEXT)):
        if role in names:
            lines.append("  SCSI ID %d  %-22s %s" % (SCSI_ID[role], names[role], what))
    lines.append("")
    if sel["target"] == "card":
        lines += ["Copy the .hda files to the root of the ZuluSCSI/BlueSCSI SD card;",
                  "their names give the SCSI IDs. Keep ID 7 free for the host adapter."]
    elif sel["target"] == "sd":
        lines += ["Copy the .hdf files to the hdf folder of the Z3660 SD card and give",
                  "each its SCSI unit in z3660cfg.txt as listed above. Write them to an",
                  "empty card: the firmware cannot use files in 32 or more pieces."]
    else:
        lines += ["%s starts them in FS-UAE." % FSUAE_CONFIG]
    lines += ["", "SHA256SUMS lists the checksum of every file."]
    return "\n".join(lines) + "\n"


_fs_cache = []


def fs_uae():
    """FS-UAE on this host, looked up once."""
    if not _fs_cache:
        _fs_cache.append(fsuae.detect())
    return _fs_cache[0]


def make_plan(cfg, paths, fs=None, written=False):
    """The ordered plan for cfg; paths: core_hdf, out_dir, kickstart, host,
    fsuae_add (default True: add "AMIX Installer" to FS-UAE's list)."""
    fs = fs or fs_uae()
    pl = Plan(cfg, paths)
    sel = cfg.sel
    src = paths.get("core_hdf")
    out = paths.get("out_dir")
    stem = os.path.splitext(os.path.basename(src))[0] if src else "amix"

    repo = paths.get("repo") or default_repo()
    no_repo = None if repo else ("no package repository: run \u201cmake repo\u201d in "
                                 "components/desktop/amix-installer, or choose one")
    if sel["src"] == "run":
        pl.actions.append(NetInstall("the running system fetches the bootstrap with tftp; "
                                     "its own pkgadd and pkg install the rest", repo, no_repo))
        return pl
    if sel["src"] == "tape":
        pl.problems.append("installing from a tape image comes later (in FS-UAE, from your "
                           "tape and root floppy)")
        return pl
    if not src:
        pl.problems.append("choose the core HDF on the Base system page")
    elif cfg.source is None or cfg.source.path != src:
        pl.problems.append("the core HDF has not been read yet")
    if not out:
        pl.problems.append("choose the folder for the new disks on the Base system page")
    else:
        try:
            safety.check_output_folder(src, out)
        except safety.Refused as e:
            pl.problems.append(str(e))
    _, fits = cfg.space()
    if not fits:
        pl.problems.append("the selection does not fit (Disks page)")
    # the foundation: every AMIX starts from 2.1p2a (FS-UAE and real machines)
    if cfg.source is not None and cfg.source.path == src and not cfg.source.patch:
        why = check_patch_disk(paths.get("patch_disk"))
        if why:
            pl.problems.append("the core disk lacks AmigaUNIX Patch Disk 1-2a: " + why)
    if pl.problems:
        return pl

    names = disk_names(sel, stem)
    if sel["layout"] != "split":
        names.pop("data")
    if sel["boot"] != "pvmk":
        names.pop("pvmk")

    if sel["layout"] == "grow":
        pl.actions.append(Later("grow-core", "Copy the core system to a larger disk",
                                "a larger %s; AMIX copies the core into it in FS-UAE"
                                % names["core"], NO_UFS))
    else:
        pl.actions.append(CopyCore(src, names["core"]))
    if "pvmk" in names:
        if cfg.source is not None and cfg.source.pvmk is not None:
            pl.actions.append(Later("pvmk", "Update the pvmk boot partition",
                                    "the core disk has one (%s)" % cfg.source.pvmk.name,
                                    "pvmk’s own “update” does it (0.4)"))
            names.pop("pvmk")
        else:
            pv = find_pvmk()
            pl.actions.append(PvmkDisk(cfg, pv, names["pvmk"]))
            if pv is None:
                # never a README or FS-UAE configuration naming a disk not written
                pl.problems.append("pvmk not found (package developer/amix/pvmk): "
                                   "%s cannot be written; choose the stock boot, install "
                                   "developer/amix/pvmk or set AMIX_PVMK_DIR" % names["pvmk"])
    ids = kernel_drivers(sel)
    if ids:
        dk = DriverKernel(src, ids, sel.get("kern", "stock"),
                          cfg.source is not None and not cfg.source.patch)
        pl.actions.append(dk)
        if dk.missing:
            # never a system whose chosen card has no driver in its kernel
            pl.problems.append("the kernel for your cards cannot be made: %s" % dk.missing)
    if "data" in names:
        pl.actions.append(DataDisk(src, names["data"], model.DATA_DISK_MB[sel["dsize"]] << 20))
    kick = paths.get("kickstart")
    if not kick and fs.a3000_kickstarts():
        kick = fs.a3000_kickstarts()[0]      # the newest A3000 ROM in FS-UAE's folder
    fsc = FsUaeConfig(names, kick, out, sel.get("gfx"))
    pl.actions.append(fsc)

    # what AMIX does in FS-UAE over the emulated A2065 (0.4)
    inside = []
    if "data" in names:
        inside.append("makes the data disk’s file system")
        if cfg.source is not None and cfg.source.movable_bytes():
            inside.append("moves " + hdf.MOVABLE_TEXT + " (%d MB) there"
                          % (cfg.source.movable_bytes() >> 20))
    if not cfg.installed(model.find_group("pkgclient").option("pkg")):
        inside.append("installs the package client")
    pkgs = []
    for s in model.STEPS:
        for g in s.groups:
            if g.id in ("pkgclient", "kern", "boot", "src", "target", "mach", "accel", "cpu",
                        "layout", "dsize", "net", "keymap", "gfx"):
                continue
            for o in g.options:
                if cfg.chosen(g, o) and o.mb and not cfg.installed(o):
                    pkgs.append(o.title)
    if pkgs:
        inside.append("installs %d packages (%s)" % (len(pkgs), ", ".join(pkgs)))
    if kernel_drivers(sel):
        from . import kernel
        ids = kernel_drivers(sel)
        inside.append("installs the kernel with the %s driver%s, %s and the card setting"
                      % (" and ".join(kernel.DRIVERS[i].card for i in ids),
                         "s" if len(ids) > 1 else "",
                         "their device nodes" if len(ids) > 1 else "its device node"))
    if sel["kern"] != "stock":
        inside.append("installs the %s kernel last, the stock kernel kept as a fallback"
                      % model.find_group("kern").option(sel["kern"]).title)
    if inside:
        why = no_repo if fs.installed else \
            "FS-UAE is not installed here (%s)" % fsuae.install_hint()
        pl.actions.append(NetInstall("next page: you start FS-UAE and type the commands shown; "
                                     "AMIX then " + "; ".join(inside), repo, why))
    if sel["target"] == "sd":
        pl.actions.append(FragCheck([n for a in pl.runnable() for n in a.outputs
                                     if n.endswith(".hdf")]))
    pl.actions.append(Checksums(cfg, readme(cfg, names)))
    if fs.installed and paths.get("fsuae_add", True) and fsc.available and \
            not fs.has_config():
        pl.actions.append(FsUaeRegister(fs, fsc.text))

    need = sum(a.size for a in pl.runnable())
    if written:
        # the disks of an earlier run, for the network install: they must be there
        for a in pl.runnable():
            for name in a.outputs:
                if not os.path.exists(os.path.join(out, name)):
                    pl.problems.append("%s is missing: write the disks first"
                                       % os.path.join(out, name))
        return pl
    try:
        st = os.statvfs(out)
        free = st.f_bavail * st.f_frsize
        if free < need + (64 << 20):
            pl.problems.append("the output folder has %d MB free; the disks need %d MB"
                               % (free >> 20, need >> 20))
    except OSError as e:
        pl.problems.append("cannot check free space in %s: %s" % (out, e))
    for a in pl.runnable():
        for name in a.outputs:
            for p in (os.path.join(out, name), os.path.join(out, name + ".part")):
                if os.path.exists(p):
                    pl.problems.append("%s exists; the installer writes new files only" % p)
                elif src and os.path.realpath(p) == os.path.realpath(src):
                    pl.problems.append("%s is the source image" % p)
    return pl


# --- running ---------------------------------------------------------------

def leftover(out_dir):
    """Files an interrupted run left behind (from its journal): its .part files
    and the disks it had finished; the plan had refused every existing name."""
    j = os.path.join(out_dir, JOURNAL)
    if not os.path.exists(j):
        return []
    with open(j) as f:
        names = [ln.strip() for ln in f if ln.strip()]
    return [n for n in names if "/" not in n and n not in (".", "..")]


def clean_leftover(out_dir):
    for n in leftover(out_dir):
        p = os.path.join(out_dir, n)
        if os.path.exists(p):
            os.remove(p)
    j = os.path.join(out_dir, JOURNAL)
    if os.path.exists(j):
        os.remove(j)


def execute(pl, progress=None, log=None, cancel=None):
    """Run the plan's available actions in order.  Returns {name: sha256}.
    On error or cancel everything this run created is removed again (its .part
    files and the files it finished): they are new files, and half a set of
    disks is of no use."""
    if pl.problems:
        raise safety.Refused("; ".join(pl.problems))
    out = pl.paths["out_dir"]
    if leftover(out):
        raise safety.Refused("an earlier run was interrupted in %s; clean it up first" % out)
    acts = pl.runnable()
    names = [n for a in acts for n in a.outputs]
    parts = [n + ".part" for n in names]
    j = os.path.join(out, JOURNAL)
    with open(j, "x") as f:
        f.write("\n".join(parts + names) + "\n")
        f.flush()
        os.fsync(f.fileno())
    total = float(sum(max(a.size, 1) for a in acts))
    base = [0.0]
    results = {}

    def prog(fr, text, a=None):
        if progress:
            progress((base[0] + fr * max(a.size, 1)) / total, a.title, text)

    try:
        for a in acts:
            ctx = Context(out, lambda f, t, a=a: prog(f, t, a), log, cancel)
            ctx.results = results
            if log:
                log("== %s" % a.title)
            ctx.progress(0.0, "")
            a.run(ctx)
            base[0] += max(a.size, 1)
    except BaseException:
        # the plan refused existing names, so each of these is this run's own
        for n in parts + names:
            p = os.path.join(out, n)
            if os.path.exists(p):
                os.remove(p)
        os.remove(j)
        raise
    os.remove(j)
    return results
