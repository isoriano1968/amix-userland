"""Is a newer AMIX Installer published?  And the update itself, always
through the system's package manager, never by copying files.

OpenIndiana: the package desktop/amix-installer (`pkg list -nH` against its
publisher's catalog); `pfexec pkg update desktop/amix-installer` updates it.
Until it is in the OpenIndiana repository, the oi-amix GitHub release is
the fallback source of the version and its notes.

Debian and Ubuntu: there is no apt repository; the newest linux-amix GitHub
release is the source.  The update downloads amix-installer_<v>_all.deb from
it, checks it against the release's SHA256SUMS (any mismatch: nothing is
installed) and installs it with `pkexec apt-get install`, so apt resolves
its dependencies.

Offline, or when anything goes wrong while checking, check() answers None
and the window says nothing.  Settings (~/.config/amix-installer/update.json):
"check" (default on) and "later", a version the user put off (until the
next one)."""
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
import urllib.request

from . import __version__

FMRI = "desktop/amix-installer"
RELEASES = {"oi": "isoriano1968/oi-amix", "deb": "isoriano1968/linux-amix"}
API = "https://api.github.com/repos/%s/releases/latest"
TIMEOUT = 15
SETTINGS = os.path.expanduser("~/.config/amix-installer/update.json")
DAY = 24 * 3600


class UpdateError(Exception):
    pass


class Update(object):
    """A newer installer: version, the release notes (markdown, may be ""),
    kind ("oi" or "deb"), and for a .deb its download and SHA256SUMS URLs."""

    def __init__(self, version, kind, notes="", url=None, deb=None, sums=None):
        self.version, self.kind, self.notes = version, kind, notes
        self.url, self.deb, self.sums = url, deb, sums


def vkey(v):
    """0.9.0 < 0.9.1 < 0.10.0; "0.9.0-2026.0.0.1" and "0.9.0-1" count as 0.9.0."""
    return tuple(int(x) for x in re.findall(r"\d+", (v or "").split("-")[0].split(",")[0]))


def settings():
    try:
        with open(SETTINGS) as f:
            d = json.load(f)
    except (OSError, ValueError):
        d = {}
    return {"check": bool(d.get("check", True)), "later": d.get("later")}


def save_settings(d):
    try:
        os.makedirs(os.path.dirname(SETTINGS), exist_ok=True)
        with open(SETTINGS + ".new", "w") as f:
            json.dump(d, f)
        os.replace(SETTINGS + ".new", SETTINGS)
    except OSError:
        pass


def kind(run=subprocess.run):
    """How this installer was installed: "oi" (IPS), "deb" (dpkg) or None
    (a source tree: nothing to update through a package manager)."""
    try:
        if shutil.which("pkg") and os.path.isdir("/var/pkg"):
            if run(["pkg", "list", "-H", FMRI], stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, timeout=60).returncode == 0:
                return "oi"
        if shutil.which("dpkg-query"):
            r = run(["dpkg-query", "-W", "-f", "${Status}", "amix-installer"],
                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, universal_newlines=True,
                    timeout=30)
            if r.returncode == 0 and "installed" in r.stdout:
                return "deb"
    except (OSError, subprocess.SubprocessError):
        pass
    return None


def _release(repo, opener):
    with opener(urllib.request.Request(API % repo, headers={"Accept": "application/vnd.github+json"}),
                timeout=TIMEOUT) as r:
        return json.load(r)


def _pkg_newest(run):
    """The newest desktop/amix-installer in the publisher's catalog, or None."""
    r = run(["pkg", "list", "-nH", FMRI], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            universal_newlines=True, timeout=120)
    if r.returncode != 0:
        return None
    for line in r.stdout.splitlines():
        w = line.split()
        if w and w[0] == FMRI and len(w) >= 2:
            return w[1]
    return None


def check(how=None, current=__version__, opener=urllib.request.urlopen, run=subprocess.run):
    """An Update newer than current, or None (no newer one, or offline)."""
    how = how or kind(run)
    if how is None:
        return None
    try:
        if how == "oi":
            v = _pkg_newest(run)
            notes, url = "", None
            try:
                rel = _release(RELEASES["oi"], opener)
                notes, url = rel.get("body") or "", rel.get("html_url")
                if not v:
                    v = (rel.get("tag_name") or "").lstrip("v")
            except Exception:
                pass
            if v and vkey(v) > vkey(current):
                return Update(v.split("-")[0], "oi", notes, url)
            return None
        rel = _release(RELEASES["deb"], opener)
        deb = sums = None
        for a in rel.get("assets") or []:
            if re.match(r"amix-installer_[0-9.]+-[0-9]+_all\.deb$", a.get("name", "")):
                deb = a
            elif a.get("name") == "SHA256SUMS":
                sums = a
        if not deb or not sums:
            return None
        v = deb["name"].split("_")[1]
        if vkey(v) > vkey(current):
            return Update(v.split("-")[0], "deb", rel.get("body") or "", rel.get("html_url"),
                          (deb["name"], deb["browser_download_url"]), sums["browser_download_url"])
    except Exception:
        return None
    return None


def wanted(u, st=None):
    """Show u?  Not when checking is off, or the user put this version off."""
    st = st or settings()
    return bool(u) and st["check"] and st.get("later") != u.version


def fetch_deb(u, dest, opener=urllib.request.urlopen):
    """The release's .deb in dest, checked against its SHA256SUMS; its path."""
    name, url = u.deb
    with opener(u.sums, timeout=TIMEOUT) as r:
        sums = r.read().decode("utf-8", "replace")
    want = None
    for line in sums.splitlines():
        w = line.split()
        if len(w) == 2 and w[1].lstrip("*") == name:
            want = w[0].lower()
    if not want:
        raise UpdateError("%s is not in the release's SHA256SUMS: nothing installed" % name)
    path = os.path.join(dest, name)
    h = hashlib.sha256()
    with opener(url, timeout=120) as r, open(path, "wb") as f:
        for chunk in iter(lambda: r.read(1 << 16), b""):
            h.update(chunk)
            f.write(chunk)
    if h.hexdigest() != want:
        os.unlink(path)
        raise UpdateError("%s: its sha256 is not the one in SHA256SUMS: nothing installed" % name)
    return path


def command(u, deb_path=None):
    if u.kind == "oi":
        return ["pfexec", "pkg", "update", FMRI]
    return ["pkexec", "apt-get", "install", "-y", "--only-upgrade", deb_path]


def apply(u, opener=urllib.request.urlopen, run=subprocess.run):
    """Update to u; (True, text) or (False, why).  pkexec's 126/127: the
    authorization was cancelled or refused."""
    tmp = tempfile.mkdtemp(prefix="amix-installer-update-")
    try:
        path = fetch_deb(u, tmp, opener) if u.kind == "deb" else None
        r = run(command(u, path), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                universal_newlines=True, timeout=1800)
        if r.returncode == 0:
            return True, "AMIX Installer %s is installed: restart it to use it" % u.version
        if u.kind == "deb" and r.returncode in (126, 127):
            return False, "The update was cancelled: nothing was changed"
        tail = (r.stdout or "").strip().splitlines()[-3:]
        return False, "The update failed: %s" % ("; ".join(tail) or "exit %d" % r.returncode)
    except UpdateError as e:
        return False, str(e)
    except (OSError, subprocess.SubprocessError) as e:
        return False, "The update failed: %s" % e
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
