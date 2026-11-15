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
