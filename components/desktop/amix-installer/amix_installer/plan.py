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

from . import __version__, hdf, model, safety

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


def find_pvmk():
    """A pvmk build: bootblock.bin, pvmk.bin, host/mkadf, host/mkhdf (and its conf/)."""
    cands = [os.environ.get("AMIX_PVMK_DIR"),
             os.path.expanduser("~/Sources/usr-amix/pvmk/build")]
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cands.append(os.path.join(os.path.dirname(os.path.dirname(here)), "share", "amix-installer", "pvmk"))
    for d in cands:
        if d and all(os.path.exists(os.path.join(d, f)) for f in
                     ("bootblock.bin", "pvmk.bin", "host/mkadf", "host/mkhdf")):
            return d
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
    for k, v in want.items():
        if k not in seen:
            out.append("%s=%s" % (k, v))
    return "\n".join(out) + "\n"


class PvmkDisk(Action):
    """pvmk's boot volume with our pvmk.conf, as an RDB disk of its own
    (pvmk.hdf: on the Z3660 SD card, SCSI unit 5)."""

    def __init__(self, cfg, pvdir, name="pvmk.hdf"):
        missing = None if pvdir else ("no pvmk build found (bootblock.bin, pvmk.bin, mkadf, "
                                      "mkhdf); set AMIX_PVMK_DIR")
        super(PvmkDisk, self).__init__(
            "pvmk", "Write the pvmk boot disk",
            "%s: boot menu entry AMIX = UNIX_Root:/stand/unix, keymap %s%s" %
            (name, cfg.sel["keymap"], ", z3reloc" if cfg.sel["accel"] == "z3660" else ""),
            1 << 20, [name], missing)
        self.cfg = cfg
        self.pvdir = pvdir
        self.name = name

    def run(self, ctx):
        d = self.pvdir
        tmp = tempfile.mkdtemp(prefix="amix-installer-")
        try:
            conf = os.path.join(tmp, "pvmk.conf")
            with open(conf, "w") as f:
                f.write(pvmk_conf(self.cfg, os.path.join(d, "..", "conf", "pvmk.conf")))
            adf = os.path.join(tmp, "pvmk.adf")
            args = [os.path.join(d, "host", "mkadf"), "-q", "-o", adf,
                    "-b", os.path.join(d, "bootblock.bin"), "-i", os.path.join(d, "pvmk.bin"),
                    "-n", "PVMK", "-f", "pvmk.conf=" + conf]
            readme = os.path.join(d, "..", "conf", "README.disk")
            if os.path.exists(readme):
                args += ["-f", "README=" + readme]
            ctx.progress(0.1, "making the boot volume")
            run(args, ctx)
            run([os.path.join(d, "host", "mkadf"), "--verify", adf, "-q"], ctx)
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
            run([os.path.join(d, "host", "mkhdf"), "-o", part,
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
    def __init__(self, cfg):
        super(Checksums, self).__init__(
            "sums", "Write checksums and the record of this run",
            "SHA256SUMS and amix-installer.json next to the disks", 4096,
            ["SHA256SUMS", "amix-installer.json"])
        self.cfg = cfg

    def run(self, ctx):
        from . import profile
        sums = "".join("%s  %s\n" % (h, n) for n, h in sorted(ctx.results.items()))
        rec = {"installer": __version__, "time": time.strftime("%Y-%m-%dT%H:%M:%S"),
               "profile": profile.dump(self.cfg), "files": ctx.results}
        for name, text in (("SHA256SUMS", sums),
                           ("amix-installer.json", json.dumps(rec, indent=2, sort_keys=True) + "\n")):
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


# AMIX writes its own file system: these steps run on the machine after its
# first boot, over the network (version 0.4)
NO_UFS = "AMIX does this itself after the first boot, over the network (0.4)"
NO_PKG = "AMIX does this with its package client after the first boot (0.4)"


def make_plan(cfg, paths):
    """The ordered plan for cfg; paths: core_hdf, out_dir, host."""
    pl = Plan(cfg, paths)
    sel = cfg.sel
    src = paths.get("core_hdf")
    out = paths.get("out_dir")
    stem = os.path.splitext(os.path.basename(src))[0] if src else "amix"

    if sel["src"] == "run":
        pl.problems.append("changing a running system comes in version 0.4")
        return pl
    if sel["src"] == "tape":
        pl.problems.append("installing from a tape image comes last (it needs an emulator)")
        return pl
    if sel["target"] == "scsi":
        pl.problems.append("writing a SCSI or BlueSCSI disk comes in version 0.5; "
                           "choose HDF files or the Z3660 SD card")
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
    if pl.problems:
        return pl

    core_name = "%s-core.hdf" % stem
    if sel["layout"] == "grow":
        pl.actions.append(Later("grow-core", "Copy the core system to a larger disk",
                                "a new %s with the core copied into a larger UNIX_Root"
                                % core_name, NO_UFS))
    else:
        pl.actions.append(CopyCore(src, core_name))
    if sel["boot"] == "pvmk":
        if cfg.source is not None and cfg.source.pvmk is not None:
            pl.actions.append(Later("pvmk", "Update the pvmk boot partition",
                                    "the core disk has one (%s); updating it in the copy"
                                    % cfg.source.pvmk.name, "pvmk's own \u201cupdate\u201d does "
                                    "it after the first boot (0.4)"))
        else:
            pl.actions.append(PvmkDisk(cfg, find_pvmk()))
    if sel["layout"] == "split":
        mb = model.DATA_DISK_MB[sel["dsize"]]
        pl.actions.append(Later("data-disk", "Create the data disk",
                                "%s-data.hdf, %d MB: RDB and an empty UFS for /usr/local, "
                                "/usr/gnu and /opt" % (stem, mb), NO_UFS, mb << 20))
        if cfg.source is not None and cfg.source.movable_bytes():
            pl.actions.append(Later("move", "Move /usr/local, /usr/gnu and /opt to the data disk",
                                    "%d MB, links left in their place"
                                    % (cfg.source.movable_bytes() >> 20), NO_UFS))
    if not cfg.installed(model.find_group("pkgclient").option("pkg")):
        pl.actions.append(Later("pkg-client", "Install the package client",
                                "pkg and /var/pkg in the copy, from its SVR4 datastream", NO_PKG))
    if sel["kern"] != "stock":
        k = model.find_group("kern").option(sel["kern"])
        pl.actions.append(Later("kernel", "Install the kernel",
                                "%s as /stand/unix; the stock kernel stays as a fallback boot entry" % k.title,
                                NO_PKG))
    pkgs = []
    for s in model.STEPS:
        for g in s.groups:
            if g.id in ("pkgclient", "kern", "boot", "src", "target", "mach", "accel", "cpu",
                        "layout", "dsize"):
                continue
            for o in g.options:
                if cfg.chosen(g, o) and o.mb and not cfg.installed(o):
                    pkgs.append(o.title)
    if pkgs:
        pl.actions.append(Later("packages", "Install %d packages" % len(pkgs), ", ".join(pkgs),
                                NO_PKG))
    if sel["target"] == "sd":
        pl.actions.append(FragCheck([n for a in pl.runnable() for n in a.outputs
                                     if n.endswith(".hdf")]))
    pl.actions.append(Checksums(cfg))

    need = pl.write_bytes()
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
