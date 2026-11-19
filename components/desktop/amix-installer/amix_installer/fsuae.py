"""FS-UAE on this host: is it installed, where is its folder, its Kickstarts,
and is there an "AMIX Installer" configuration in its list.

FS-UAE keeps everything in one base folder: $FS_UAE_BASE_DIR, else
"FS-UAE" in the user's documents folder (xdg-user-dir DOCUMENTS), else
~/Documents/FS-UAE or ~/FS-UAE. Configurations live in its Configurations
folder; the launcher lists every *.fs-uae there.
"""
import os
import re
import shutil
import subprocess

CONFIG_NAME = "AMIX Installer"

INSTALL_HINT = {
    "debian": "apt install fs-uae fs-uae-launcher",
    "openindiana": "pkg install emulators/fs-uae emulators/fs-uae-launcher",
}


class FsUae(object):
    def __init__(self):
        self.binary = None
        self.launcher = None
        self.version = None
        self.base = None

    @property
    def installed(self):
        return self.binary is not None

    def folder(self, name):
        return os.path.join(self.base, name) if self.base else None

    def configurations(self):
        d = self.folder("Configurations")
        if not d or not os.path.isdir(d):
            return []
        return sorted(f[:-7] for f in os.listdir(d) if f.endswith(".fs-uae"))

    def config_path(self, name=CONFIG_NAME):
        d = self.folder("Configurations")
        return os.path.join(d, name + ".fs-uae") if d else None

    def has_config(self, name=CONFIG_NAME):
        p = self.config_path(name)
        return bool(p and os.path.exists(p))

    def network_of(self, path):
        """The network card a configuration gives the Amiga, or None: the
        network install needs network_card = a2065 (FS-UAE's slirp NAT)."""
        try:
            with open(path, errors="replace") as f:
                for line in f:
                    m = re.match(r"\s*network_card\s*=\s*(\S+)", line)
                    if m:
                        return m.group(1).lower()
        except OSError:
            pass
        return None

    def a3000_kickstarts(self):
        """A3000 ROMs in the Kickstarts folder, newest version first."""
        d = self.folder("Kickstarts")
        if not d or not os.path.isdir(d):
            return []
        roms = []
        for f in os.listdir(d):
            m = re.match(r"(?i)^(?:a3000\.(\d+)\.(\d+)|kicka3000)\.rom$", f)
            if m:
                key = (int(m.group(1)), int(m.group(2))) if m.group(1) else (0, 0)
                roms.append((key, os.path.join(d, f)))
        return [p for _, p in sorted(roms, reverse=True)]


def _documents():
    try:
        out = subprocess.run(["xdg-user-dir", "DOCUMENTS"], stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, universal_newlines=True,
                             timeout=5).stdout.strip()
        if out and out != os.path.expanduser("~"):
            return out
    except (OSError, subprocess.SubprocessError):
        pass
    return os.path.expanduser("~/Documents")


def base_dir():
    env = os.environ.get("FS_UAE_BASE_DIR")
    if env:
        return env
    for d in (os.path.join(_documents(), "FS-UAE"), os.path.expanduser("~/Documents/FS-UAE"),
              os.path.expanduser("~/FS-UAE")):
        if os.path.isdir(d):
            return d
    return os.path.join(_documents(), "FS-UAE")


def detect():
    f = FsUae()
    f.binary = shutil.which("fs-uae")
    f.launcher = shutil.which("fs-uae-launcher")
    if f.binary:
        try:
            out = subprocess.run([f.binary, "--version"], stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, universal_newlines=True,
                                 timeout=10).stdout
            m = re.search(r"\d+\.\d+\.\d+", out)
            f.version = m.group(0) if m else None
        except (OSError, subprocess.SubprocessError):
            pass
    f.base = base_dir()
    return f


def install_hint():
    text = ""
    for f in ("/etc/os-release", "/etc/release"):
        try:
            with open(f) as fh:
                text += fh.read().lower()
        except OSError:
            pass
    return INSTALL_HINT["openindiana" if "openindiana" in text else "debian"]


# --- the disks of a configuration: the AMIX core, and a data disk ------------

DATA_SCSI_ID = 1                    # as the installer's own disks (plan.SCSI_ID)
KEPT_SUFFIX = ".before-amix-installer"


def drives(path):
    """{n: {"path": ..., "controller": ...}} of the hard_drive_N lines of an
    FS-UAE configuration; relative paths as FS-UAE takes them (its Hard Drives
    folder, else beside the configuration)."""
    out = {}
    with open(path, errors="replace") as f:
        for line in f:
            m = re.match(r"\s*hard_drive_(\d+)(_controller)?\s*=\s*(.*?)\s*$", line)
            if m:
                d = out.setdefault(int(m.group(1)), {})
                d["controller" if m.group(2) else "path"] = m.group(3)
    for d in out.values():
        p = os.path.expanduser(d.get("path", "").replace("$HOME", "~"))
        if p and not os.path.isabs(p):
            cands = [os.path.join(base_dir(), "Hard Drives", p),
                     os.path.join(os.path.dirname(os.path.abspath(path)), p)]
            p = next((c for c in cands if os.path.exists(c)), cands[-1])
        d["path"] = p
    return out


def _scsi_id(controller):
    m = re.match(r"scsi(\d)$", controller or "")
    return int(m.group(1)) if m else None


def amix_disks(path):
    """What an FS-UAE configuration holds for AMIX: (core, data, data_id_owner).
    core: the image whose UNIX root has /etc/passwd, or None.  data: (image,
    sectors, empty) of the AMIX disk at SCSI ID 1 (empty: no file system yet,
    AMIX makes it), or None.  data_id_owner: an image at SCSI ID 1 that is not
    an AMIX disk (then no data disk can be added there), or None."""
    from . import hdf
    core = data = owner = None
    for n, d in sorted(drives(path).items()):
        p = d.get("path")
        if not p or not os.path.isfile(p):
            continue
        try:
            with hdf.Image(p) as im:
                parts = im.read_rdb()
                root = next((x for x in parts if x.kind == "amix-root"), None)
                if root is None:
                    if _scsi_id(d.get("controller")) == DATA_SCSI_ID:
                        owner = p
                    continue
                try:
                    fs = hdf.Ufs(im, root)
                    has_fs, is_core = True, fs.exists("/etc/passwd")
                except (hdf.UfsError, OSError, ValueError):
                    has_fs, is_core = False, False
        except (OSError, hdf.NotAnImage):
            if _scsi_id(d.get("controller")) == DATA_SCSI_ID:
                owner = p
            continue
        if is_core and core is None:
            core = p
        elif _scsi_id(d.get("controller")) == DATA_SCSI_ID:
            data = (p, root.blocks, not has_fs)
    return core, data, owner


def add_data_disk(path, size_bytes, out=None):
    """An empty AMIX data disk for the configuration at path: the partition
    table from its core disk (rdb.make_data_disk), written beside it (or to
    out), added as the next hard_drive_N on SCSI ID 1.  The configuration is
    kept once as <path>.before-amix-installer.  Returns the new image.  Raises
    ValueError when the configuration has no AMIX core disk, already has a
    data disk, SCSI ID 1 is taken, or the image exists."""
    from . import rdb
    core, data, owner = amix_disks(path)
    if core is None:
        raise ValueError("%s has no AMIX core disk" % path)
    if data is not None:
        raise ValueError("%s has a data disk already: %s" % (path, data[0]))
    if owner is not None:
        raise ValueError("SCSI ID %d is taken by %s" % (DATA_SCSI_ID, owner))
    ids = [_scsi_id(d.get("controller")) for d in drives(path).values()]
    if DATA_SCSI_ID in ids:
        raise ValueError("SCSI ID %d is taken in %s" % (DATA_SCSI_ID, path))
    if out is None:
        stem = os.path.splitext(os.path.basename(core))[0]
        stem = stem[:-5] if stem.endswith("-core") else stem
        out = os.path.join(os.path.dirname(core), stem + "-data.hdf")
    if os.path.exists(out):
        raise ValueError("%s exists already" % out)
    rdb.make_data_disk(core, out, size_bytes)
    n = max(list(drives(path)) + [-1]) + 1
    kept = path + KEPT_SUFFIX
    if not os.path.exists(kept):
        shutil.copy2(path, kept)
    with open(path, errors="replace") as f:
        text = f.read()
    if text and not text.endswith("\n"):
        text += "\n"
    text += ("# the AMIX data disk (AMIX Installer): AMIX makes its file system\n"
             "hard_drive_%d = %s\nhard_drive_%d_controller = scsi%d\nhard_drive_%d_type = rdb\n"
             % (n, out, n, DATA_SCSI_ID, n))
    tmp = path + ".new"
    with open(tmp, "w") as f:
        f.write(text)
    os.replace(tmp, path)
    return out
