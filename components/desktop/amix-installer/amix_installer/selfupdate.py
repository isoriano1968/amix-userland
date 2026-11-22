"""Is a newer AMIX Installer published?  And the update itself, always
through the system's package manager, never by copying files.

OpenIndiana: the newer of two sources, the publisher's catalog (`pkg list
-nH`) and the newest oi-amix GitHub release.  The catalog always knows at
least the installed version (a package installed with `pkg install -g` has
no other), so its answer alone never means that nothing newer exists.  From
the catalog, `pfexec pkg update desktop/amix-installer` updates it.  When
only the release has the newer one, its .p5p is downloaded, checked against
its SHA256SUMS and given to that command with -g: an origin for this one
command, the publishers stay as they are.

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
    kind ("oi" or "deb"), and what a release has to download: the .deb or the
    .p5p (name, URL) and the SHA256SUMS URL.  Neither: the catalog has it."""

    def __init__(self, version, kind, notes="", url=None, deb=None, sums=None, p5p=None):
        self.version, self.kind, self.notes = version, kind, notes
        self.url, self.deb, self.sums, self.p5p = url, deb, sums, p5p


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
    found = []                          # a line for each publisher: "FMRI [(publisher)] VERSION IFO"
    for line in r.stdout.splitlines():
        w = [x for x in line.split() if not x.startswith("(")]
        if len(w) >= 2 and w[0] == FMRI:
            found.append(w[1])
    return max(found, key=vkey) if found else None


def _assets(rel, pattern):
    """(the asset whose name matches pattern, the SHA256SUMS URL), or None."""
    found = sums = None
    for a in rel.get("assets") or []:
        if re.match(pattern, a.get("name", "")):
            found = a
        elif a.get("name") == "SHA256SUMS":
            sums = a
    if found and sums:
        return (found["name"], found["browser_download_url"]), sums["browser_download_url"]
    return None


def check(how=None, current=__version__, opener=urllib.request.urlopen, run=subprocess.run):
    """An Update newer than current, or None (no newer one, or offline)."""
    how = how or kind(run)
    if how is None:
        return None
    try:
        if how == "oi":
            v = _pkg_newest(run)
            notes, url, p5p, sums = "", None, None, None
            try:
                rel = _release(RELEASES["oi"], opener)
                notes, url = rel.get("body") or "", rel.get("html_url")
                released = (rel.get("tag_name") or "").lstrip("v")
                got = _assets(rel, r"oi-amix-[0-9.]+\.p5p$")
                if got and vkey(released) > vkey(v):
                    v, (p5p, sums) = released, got
            except Exception:
                pass                    # no release to see: the catalog alone
            if v and vkey(v) > vkey(current):
                return Update(v.split("-")[0], "oi", notes, url, sums=sums, p5p=p5p)
            return None
        rel = _release(RELEASES["deb"], opener)
        got = _assets(rel, r"amix-installer_[0-9.]+-[0-9]+_all\.deb$")
        if not got:
            return None
        deb, sums = got
        v = deb[0].split("_")[1]
        if vkey(v) > vkey(current):
            return Update(v.split("-")[0], "deb", rel.get("body") or "", rel.get("html_url"),
                          deb, sums)
    except Exception:
        return None
    return None


def wanted(u, st=None):
    """Show u?  Not when checking is off, or the user put this version off."""
    st = st or settings()
    return bool(u) and st["check"] and st.get("later") != u.version


def fetch(u, dest, opener=urllib.request.urlopen):
    """The release's .deb or .p5p in dest, checked against SHA256SUMS; its path."""
    name, url = u.deb or u.p5p
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


def command(u, path=None):
    if u.kind == "oi":
        return ["pfexec", "pkg", "update"] + (["-g", path] if path else []) + [FMRI]
    return ["pkexec", "apt-get", "install", "-y", "--only-upgrade", path]


def apply(u, opener=urllib.request.urlopen, run=subprocess.run):
    """Update to u; (True, text) or (False, why).  pkexec's 126/127: the
    authorization was cancelled or refused."""
    # the .p5p is about 800 MB, and /tmp on OpenIndiana is swap
    where = "/var/tmp" if u.p5p and os.path.isdir("/var/tmp") else None
    tmp = tempfile.mkdtemp(prefix="amix-installer-update-", dir=where)
    try:
        path = fetch(u, tmp, opener) if u.deb or u.p5p else None
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
