"""Build AMIX packages on this computer from amix-userland (stage 2 of the
package-origin plan): the components that are new or newer than the
repository the installer serves are built with the toolchain installed here,
pass the same gates as every published package (tools/amix-p5m and
tools/amix-publish of that very source: shared library dependencies, the
files a package uses, the CPU scan, a path two packages deliver, a new
revision for changed content) and land in the local repository, which Serve
Packages then serves.

The source is a local amix-userland checkout, or a git URL (cloned into the
cache; a component's revision is its commit count, so it must be git, not a
tarball).  The toolchain: AMIX_TOOLS, else /opt/m68k-amix/bin (the OI package
developer/amix/gcc-14), else ~/opt/m68k-amix/bin.  The sysroot of a build is
a copy of the installed base sysroot (developer/amix/sysroot:
/opt/m68k-amix-sysroot, or AMIX_SYSROOT_BASE) with the packages the
component requires unpacked into it from the repository, newest first; never
a build tree of somebody's own.

The local repository (~/.local/share/amix-installer/repo) starts as a copy
of the bundled one and keeps what is built here; packages of a newer
bundled repository are merged in.
"""
import gzip
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.parse

from . import plan

CACHE = os.path.expanduser(os.environ.get("AMIX_INSTALLER_CACHE", "~/.cache/amix-installer"))
LOCAL_REPO = os.path.expanduser(os.environ.get("AMIX_LOCAL_REPO", "~/.local/share/amix-installer/repo"))
DEFAULT_SOURCE = "https://github.com/isoriano1968/amix-userland.git"
PUBLISHER = "amix"
SYSROOT_MARK = "etc/amix-sysroot.release"
# host components (the manifest's "host" key): programs of the PC, packaged
# for OpenIndiana or Debian, never AMIX packages.  Known ones, for a local
# repository that was copied from a bundled one that still had them
HOST_STEMS = ("desktop/amix-installer", "developer/gcc-14", "developer/gcc-27",
              "developer/vscode", "system/pvmk")
HOST_HINT = {"desktop/amix-installer": "a host app: update it with pkg (OpenIndiana) or apt (Debian)"}


class BuildError(Exception):
    pass


def _say(log, s):
    if log:
        log(s)


# -- the host's toolchain and base sysroot ------------------------------------

def toolchain():
    """The directory with m68k-elf-gcc, or None."""
    for d in (os.environ.get("AMIX_TOOLS"), "/opt/m68k-amix/bin", os.path.expanduser("~/opt/m68k-amix/bin")):
        if d and os.access(os.path.join(d, "m68k-elf-gcc"), os.X_OK):
            return d
    return None


def toolchain27():
    """gcc 2.7.2.3 (m68k-cbm-sysv4-gcc, developer/amix/gcc-27), or None: the
    compiler AMIX itself was built with.  Kernels and drivers need it (the
    installer's kernel path builds them, never this tab); among the
    components only x11/x11r6.3, whose Makefiles imake writes for its tools."""
    for d in (os.environ.get("GCC27_TOOLS"), "/opt/amix-cross/bin", os.path.expanduser("~/opt/amix-cross/bin")):
        if d and os.access(os.path.join(d, "m68k-cbm-sysv4-gcc"), os.X_OK):
            return d
    return None


def needs_gcc27(comp_dir):
    """Does the component's Makefile name gcc 2.7's tools (GCC27_TOOLS)?"""
    try:
        with open(os.path.join(comp_dir, "Makefile")) as f:
            return "GCC27_TOOLS" in f.read()
    except OSError:
        return False


def base_sysroot():
    """The installed AMIX base sysroot (its marker file present), or None."""
    for d in (os.environ.get("AMIX_SYSROOT_BASE"), "/opt/m68k-amix-sysroot",
              os.path.expanduser("~/opt/m68k-amix-sysroot")):
        if d and os.path.isfile(os.path.join(d, SYSROOT_MARK)):
            return d
    return None


def gmake():
    for m in ("gmake", "make"):
        p = shutil.which(m)
        if p:
            try:
                out = subprocess.run([p, "--version"], stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, universal_newlines=True).stdout
            except OSError:
                continue
            if "GNU Make" in out:
                return p
    return None


def problems():
    """What this computer lacks for a build, as sentences."""
    out = []
    if not toolchain():
        out.append("no AMIX cross compiler: install developer/amix/gcc-14 (or set AMIX_TOOLS)")
    if not base_sysroot():
        out.append("no AMIX base sysroot: install developer/amix/sysroot (or set AMIX_SYSROOT_BASE)")
    if not gmake():
        out.append("no GNU make (gmake)")
    if not shutil.which("git"):
        out.append("no git")
    return out


# -- the source ------------------------------------------------------------------

def is_checkout(d):
    return os.path.isfile(os.path.join(d, "tools", "amix-p5m")) and os.path.isdir(os.path.join(d, "components"))


def is_master(url):
    """Is url the amix-userland repository (GitHub, or a copy named so)?"""
    u = url.rstrip("/")
    if u.endswith(".git"):
        u = u[:-4]
    return u.rsplit("/", 1)[-1] == "amix-userland" or u.rsplit(":", 1)[-1].endswith("/amix-userland")


def pull(d, log=None):
    """A local checkout brought up to its upstream: git pull --ff-only, which
    keeps edits not committed and refuses a branch that went its own way.
    Returns a line for the log; never fails the caller."""
    if not os.path.isdir(os.path.join(d, ".git")) or not shutil.which("git"):
        return "%s: not a git checkout: used as it is" % d
    up = subprocess.run(["git", "-C", d, "rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}"],
                        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, universal_newlines=True)
    if up.returncode != 0:
        msg = "%s: its branch follows no upstream, not amix-userland: used as it is" % d
        _say(log, msg)
        return msg
    # amix-userland is the master repository: a checkout that follows another
    # one is used as it is, and said so
    remote = up.stdout.strip().split("/", 1)[0]
    url = subprocess.run(["git", "-C", d, "remote", "get-url", remote], stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, universal_newlines=True).stdout.strip()
    if not is_master(url):
        msg = ("%s follows %s, not amix-userland (the master repository): not pulled, used as "
               "it is" % (d, url or remote))
        _say(log, msg)
        return msg
    old = subprocess.run(["git", "-C", d, "rev-parse", "--short", "HEAD"], stdout=subprocess.PIPE,
                         universal_newlines=True).stdout.strip()
    r = subprocess.run(["git", "-C", d, "pull", "--ff-only", "--quiet"], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, universal_newlines=True)
    new = subprocess.run(["git", "-C", d, "rev-parse", "--short", "HEAD"], stdout=subprocess.PIPE,
                         universal_newlines=True).stdout.strip()
    if r.returncode != 0:
        msg = "git pull in %s failed (%s): used as it is" % (d, (r.stdout.strip().splitlines() or ["?"])[-1])
    elif old != new:
        msg = "git pull: %s %s..%s (%s)" % (d, old, new, up.stdout.strip())
    else:
        msg = "git pull: %s is up to date (%s, %s)" % (d, new, up.stdout.strip())
    _say(log, msg)
    return msg


def source_dir(src, log=None, update=False):
    """A local checkout (git pull --ff-only first when update), or a git
    URL[#ref] cloned, else fetched, into the cache; returns the directory."""
    src = src or DEFAULT_SOURCE
    if os.path.isdir(os.path.expanduser(src)):
        d = os.path.abspath(os.path.expanduser(src))
        if not is_checkout(d):
            raise BuildError("%s is not an amix-userland checkout (no tools/amix-p5m)" % d)
        if update:
            pull(d, log)
        return d
    url, _, ref = src.partition("#")
    name = re.sub(r"[^A-Za-z0-9_.-]+", "_", url.split("://", 1)[-1]).strip("_")
    d = os.path.join(CACHE, "src", name)
    if not os.path.isdir(os.path.join(d, ".git")):
        os.makedirs(os.path.dirname(d), exist_ok=True)
        _say(log, "git clone %s" % url)
        _run(["git", "clone", "--quiet", url, d], log)
    else:
        _say(log, "git fetch %s" % url)
        _run(["git", "-C", d, "fetch", "--quiet", "origin"], log)
    _run(["git", "-C", d, "checkout", "--quiet", "--detach", ref or "origin/HEAD"], log)
    if not is_checkout(d):
        raise BuildError("%s holds no amix-userland" % url)
    return d


def read_manifest(path):
    m = {"name": None, "version": None, "require": [], "host": None, "source": None}
    with open(path) as f:
        for line in f:
            w = line.split()
            if not w or w[0].startswith("#"):
                continue
            if w[0] in ("name", "version", "host") and len(w) > 1:
                m[w[0]] = w[1]
            elif w[0] == "source-only":
                m["source"] = " ".join(w[1:]) or "built by the components that use it"
            elif w[0] == "require" and len(w) > 1:
                m["require"].append(w[1])
    return m


def components(src, host=False, source=False):
    """stem -> {"dir", "version", "require", "source"} of every AMIX component
    with a manifest; host=True: the host components (manifest key host) as
    well; source=True: the source-only ones (manifest key source-only: built
    by the components that use them, no package of their own) as well."""
    out = {}
    base = os.path.join(src, "components")
    for top, dirs, files in os.walk(base):
        dirs[:] = [d for d in dirs if d not in ("build", "files", "patches", "tests", "src")
                   and not d.startswith(".")]
        man = [f for f in files if f.endswith(".manifest")]
        if len(man) != 1:
            continue
        m = read_manifest(os.path.join(top, man[0]))
        if (m["name"] and m["name"] == os.path.relpath(top, base) and (host or not m["host"])
                and (source or not m["source"])):
            out[m["name"]] = {"dir": top, "version": m["version"] or "0", "require": m["require"],
                              "source": m["source"]}
    return out


def host_components(src):
    """The host components of src (manifest key host)."""
    a = components(src, host=True)
    return sorted(set(a) - set(components(src)))


def resolve(src, name):
    """A component's stem from what was typed: the stem, or its last part
    (amix-installer -> desktop/amix-installer); BuildError with a hint
    when it is a host or source-only component, unknown or ambiguous."""
    every = components(src, host=True, source=True)
    amix = components(src)
    hits = [name] if name in every else sorted(s for s in every if s.split("/")[-1] == name)
    if not hits:
        near = sorted(s for s in amix if name in s)[:6]
        raise BuildError("%s: no such component%s" % (name, ("; perhaps " + ", ".join(near)) if near else ""))
    if len(hits) > 1:
        raise BuildError("%s: more than one component: %s" % (name, ", ".join(hits)))
    stem = hits[0]
    if every[stem]["source"]:
        raise BuildError("%s: source only, no package of its own (%s)" % (stem, every[stem]["source"]))
    if stem not in amix:
        raise BuildError("%s: %s" % (stem, HOST_HINT.get(stem, "a host component, not an AMIX package: "
                                                         "packaged for OpenIndiana or Debian")))
    return stem


def source_revision(src, comp_dir):
    """amix-p5m's revision: the component's commit count, ".1" when changed."""
    rel = os.path.relpath(comp_dir, src)
    try:
        n = subprocess.check_output(["git", "-C", src, "rev-list", "--count", "HEAD", "--", rel],
                                    universal_newlines=True, stderr=subprocess.DEVNULL).strip()
        dirty = (subprocess.call(["git", "-C", src, "diff", "--quiet", "HEAD", "--", rel],
                                 stderr=subprocess.DEVNULL) != 0 or
                 bool(subprocess.check_output(["git", "-C", src, "status", "--porcelain", "--", rel],
                                              universal_newlines=True, stderr=subprocess.DEVNULL).strip()))
    except (OSError, subprocess.CalledProcessError):
        return None, False
    return int(n or 0), dirty


# -- repositories ----------------------------------------------------------------

def _pkgdir(repo):
    return os.path.join(repo, "publisher", PUBLISHER, "pkg")


def repo_versions(repo):
    """stem -> its newest version ("1.3,2.1-6.1") in repo."""
    out = {}
    d = _pkgdir(repo) if repo else None
    if not d or not os.path.isdir(d):
        return out
    for qs in os.listdir(d):
        stem = urllib.parse.unquote(qs)
        vs = [urllib.parse.unquote(v).split(":")[0] for v in os.listdir(os.path.join(d, qs))]
        if vs:
            out[stem] = max(vs, key=vkey)
    return out


def _ints(s):
    return [int(x) if x.isdigit() else 0 for x in re.split(r"[.]", s)]


def vkey(v):
    """Order of "1.3,2.1-6.1": the component version, then the branch."""
    comp, _, rest = v.partition(",")
    _, _, branch = rest.partition("-")
    return (_ints(comp), _ints(branch))


def compare(src, repo, comps=None):
    """[(stem, repo version or None, source version, its revision, state)]:
    state new (not in repo), newer (a higher version), changed (more commits,
    or edits here), same."""
    comps = comps or components(src)
    have = repo_versions(repo)
    out = []
    for stem in sorted(comps):
        c = comps[stem]
        n, dirty = source_revision(src, c["dir"])
        rv = have.get(stem)
        rev = "%s%s" % (n if n is not None else "?", ".1" if dirty else "")
        if rv is None:
            state = "new"
        else:
            rcomp = rv.split(",")[0]
            rb = _ints(rv.partition("-")[2] or "0")
            rn, rk = rb[0], (rb[1] if len(rb) > 1 else 0)
            # published from edits (N.k) and those edits committed since:
            # N+1 commits, the same content (amix-publish skips it anyway)
            ahead = (n or 0) - rn - (1 if rk else 0)
            if _ints(c["version"]) > _ints(rcomp):
                state = "newer"
            elif _ints(c["version"]) == _ints(rcomp) and (dirty or ahead > 0):
                state = "changed"
            else:
                state = "same"
        out.append((stem, rv, c["version"], rev, state))
    return out


def publish_tool(src=None):
    """amix-publish: the source's, else the one shipped with the installer
    (lib/amix-installer/tools), else the source tree this installer runs from."""
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    for d in ([os.path.join(src, "tools")] if src else []) + [
            os.path.join(here, "tools"), os.path.join(here, "..", "..", "..", "tools")]:
        p = os.path.join(d, "amix-publish")
        if os.path.isfile(p):
            return os.path.abspath(p)
    return None


def bundled_repo():
    """The repository the installer brings (or AMIX_REPO, or the source
    tree's): the first usable one that is not the local repository."""
    for d in plan.repo_candidates():
        if os.path.abspath(d) == os.path.abspath(LOCAL_REPO):
            continue
        if os.path.isfile(os.path.join(d, "pkg5.repository")) and not plan.repo_problem(d):
            return d
    return None


def ensure_local_repo(bundled, src=None, log=None):
    """The local repository: a copy of the bundled one at first; the bundled
    one's newer packages merged in every time after (at Serve Packages' start
    and before a build), the catalog rebuilt by amix-publish."""
    if not os.path.isfile(os.path.join(LOCAL_REPO, "pkg5.repository")):
        if not bundled:
            raise BuildError("no bundled repository to start the local one from")
        _say(log, "the local repository: a copy of %s" % bundled)
        os.makedirs(os.path.dirname(LOCAL_REPO), exist_ok=True)
        tmp = LOCAL_REPO + ".new"
        shutil.rmtree(tmp, ignore_errors=True)
        shutil.copytree(bundled, tmp, symlinks=True)
        os.rename(tmp, LOCAL_REPO)
        drop_host(LOCAL_REPO, src, log)
        return LOCAL_REPO
    if bundled and os.path.abspath(bundled) != os.path.abspath(LOCAL_REPO):
        added = 0
        for sub in ("pkg", "file"):
            a = os.path.join(bundled, "publisher", PUBLISHER, sub)
            b = os.path.join(LOCAL_REPO, "publisher", PUBLISHER, sub)
            for top, dirs, files in os.walk(a):
                rel = os.path.relpath(top, a)
                for f in files:
                    t = os.path.join(b, rel, f)
                    if not os.path.exists(t):
                        os.makedirs(os.path.dirname(t), exist_ok=True)
                        shutil.copy2(os.path.join(top, f), t)
                        added += sub == "pkg"
        if added:
            tool = publish_tool(src)
            if not tool:
                raise BuildError("no amix-publish to rebuild %s's catalog" % LOCAL_REPO)
            _say(log, "merged %d package versions of %s" % (added, bundled))
            _run([sys.executable, tool, "-s", LOCAL_REPO, "--no-needed-check"], log)
    drop_host(LOCAL_REPO, src, log)
    return LOCAL_REPO


def drop_host(repo, src=None, log=None):
    """Host components are no AMIX packages: an old bundled repository still
    had desktop/amix-installer; out of repo with them, catalog rebuilt."""
    stems = set(HOST_STEMS) | set(host_components(src) if src else [])
    d = _pkgdir(repo)
    gone = [s for s in sorted(stems) if os.path.isdir(os.path.join(d, urllib.parse.quote(s, safe="")))]
    if not gone:
        return []
    tool = publish_tool(src)
    if not tool:
        raise BuildError("no amix-publish to rebuild %s's catalog" % repo)
    for s in gone:
        shutil.rmtree(os.path.join(d, urllib.parse.quote(s, safe="")))
    _say(log, "removed host packages from %s: %s" % (repo, ", ".join(gone)))
    _run([sys.executable, tool, "-s", repo, "--no-needed-check"], log)
    return gone


# -- the sysroot of a build ------------------------------------------------------

def requirements(comps, stem):
    """Every component stem requires, all the way down (not stem itself)."""
    seen, todo = [], list(comps.get(stem, {}).get("require", []))
    while todo:
        r = todo.pop(0)
        if r in seen or r == stem:
            continue
        seen.append(r)
        todo += comps.get(r, {}).get("require", [])
    return seen


def _newest_manifest(repo, stem):
    d = os.path.join(_pkgdir(repo), urllib.parse.quote(stem, safe=""))
    if not os.path.isdir(d):
        return None, None
    vs = sorted(os.listdir(d), key=lambda v: vkey(urllib.parse.unquote(v).split(":")[0]))
    v = vs[-1]
    with open(os.path.join(d, v)) as f:
        return urllib.parse.unquote(v), f.read()


def _attrs(words):
    a = {}
    for w in words:
        k, eq, v = w.partition("=")
        if eq:
            a[k] = v.strip('"')
    return a


def unpack(repo, stem, dest):
    """stem's newest package from repo into dest (files, links, dirs);
    returns its version, None if repo has no such package."""
    version, man = _newest_manifest(repo, stem)
    if man is None:
        return None
    files = os.path.join(repo, "publisher", PUBLISHER, "file")
    links = []
    for line in man.splitlines():
        w = line.split()
        if not w:
            continue
        a = _attrs(w[1:])
        p = a.get("path")
        if not p:
            continue
        t = os.path.join(dest, p)
        if w[0] == "dir":
            os.makedirs(t, exist_ok=True)
        elif w[0] == "file":
            h = w[1]
            os.makedirs(os.path.dirname(t), exist_ok=True)
            with gzip.open(os.path.join(files, h[:2], h), "rb") as fi, open(t + ".tmp", "wb") as fo:
                shutil.copyfileobj(fi, fo)
            os.chmod(t + ".tmp", 0o755 if a.get("mode", "0444")[-3] in "157" else 0o644)
            os.replace(t + ".tmp", t)
        elif w[0] in ("link", "hardlink") and a.get("target"):
            links.append((w[0], t, a["target"]))
    for kind, t, target in links:
        os.makedirs(os.path.dirname(t), exist_ok=True)
        if os.path.lexists(t):
            os.remove(t)
        if kind == "link":
            os.symlink(target, t)
        else:
            # a hardlink's target: absolute from the image root, or relative
            # to the link's own directory (IPS writes it so)
            src = (os.path.join(dest, target.lstrip("/")) if target.startswith("/")
                   else os.path.normpath(os.path.join(os.path.dirname(t), target)))
            if os.path.exists(src):
                os.link(src, t)
    return version


SYSROOT_PKG = "developer/amix/sysroot"


def base_paths(base):
    """The paths (relative to base) the base sysroot package delivers, when
    IPS knows them: the kits installed into the same tree (developer/amix/
    amixc99, x11r6.3, motif, cde of an older day) stay out of a build, which
    takes those libraries from the repository.  None: copy everything."""
    pkg = shutil.which("pkg")
    if not pkg or not os.path.isfile("/var/pkg/pkg5.image") and not os.path.isdir("/var/pkg"):
        return None
    try:
        out = subprocess.run([pkg, "contents", "-H", "-o", "path", SYSROOT_PKG], stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, universal_newlines=True)
    except OSError:
        return None
    if out.returncode != 0:
        return None
    pre = base.strip("/") + "/"
    return [p[len(pre):] for p in out.stdout.split() if p.startswith(pre)]


def _copy_base(base, root, paths):
    if paths is None:
        shutil.copytree(base, root, symlinks=True)
        return
    os.makedirs(root)
    for rel in paths:
        s, d = os.path.join(base, rel), os.path.join(root, rel)
        if os.path.islink(s):
            os.makedirs(os.path.dirname(d), exist_ok=True)
            os.symlink(os.readlink(s), d)
        elif os.path.isdir(s):
            os.makedirs(d, exist_ok=True)
        elif os.path.isfile(s):
            os.makedirs(os.path.dirname(d), exist_ok=True)
            shutil.copy2(s, d)


def build_sysroot(repo, comps, stem, log=None):
    """CACHE/sysroot: the base sysroot, then stem's requirements from repo."""
    base = base_sysroot()
    if not base:
        raise BuildError("no AMIX base sysroot (developer/amix/sysroot)")
    root = os.path.join(CACHE, "sysroot")
    rec = os.path.join(root, ".amix-installer.json")
    st = os.stat(os.path.join(base, SYSROOT_MARK))
    key = "%s:%d:%d" % (base, st.st_mtime, st.st_size)
    state = {}
    if os.path.isfile(rec):
        with open(rec) as f:
            state = json.load(f)
    # only the base and stem's requirements: a package an earlier build
    # unpacked and stem does not require (x11/x11r6.3 itself, say) starts
    # the sysroot again from the base
    needed = {"library/amixc99"} | set(requirements(comps, stem))
    needed.discard(stem)
    if state.get("base") == key and set(state.get("packages", {})) - needed:
        state = {}
    if state.get("base") != key:
        paths = base_paths(base)
        _say(log, "the build sysroot: %s of %s" % ("a copy" if paths is None else
                                                    "%s's %d paths" % (SYSROOT_PKG, len(paths)), base))
        shutil.rmtree(root, ignore_errors=True)
        _copy_base(base, root, paths)
        state = {"base": key, "packages": {}}
    # library/amixc99: gcc 14's specs link -lamixc99 into every program, and
    # it is the AMIX sysroot's shared library, not the toolchain's
    for r in ["library/amixc99"] + [x for x in requirements(comps, stem) if x != "library/amixc99"]:
        if r == stem:
            continue
        v = _newest_manifest(repo, r)[0]
        if v is None:
            raise BuildError("%s needs %s, which the repository does not have" % (stem, r))
        if state["packages"].get(r) != v:
            _say(log, "sysroot: %s %s" % (r, v.split(":")[0]))
            unpack(repo, r, root)
            state["packages"][r] = v
    with open(rec, "w") as f:
        json.dump(state, f)
    return root


# -- building --------------------------------------------------------------------

def _run(argv, log, cwd=None, env=None):
    p = subprocess.Popen(argv, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         universal_newlines=True, errors="replace")
    tail = []
    for line in p.stdout:
        line = line.rstrip("\n")
        tail = (tail + [line])[-30:]
        _say(log, line)
    p.stdout.close()
    if p.wait() != 0:
        raise BuildError("%s failed:\n%s" % (" ".join(os.path.basename(a) if i == 0 else a
                                                         for i, a in enumerate(argv[:4])), "\n".join(tail[-12:])))


def tool_wrappers(tools, sysroot):
    """CACHE/tools: the compiler drivers with the build sysroot's /usr/lib
    first in the library search, the other tools as they are.  gcc 14's
    specs link -lamixc99 and libc through %R, its configured sysroot (the
    installed base one, without library/amixc99's shared library).  Not
    --sysroot: with it this compiler takes SYSROOT/usr/local/include as a
    system directory after its include-fixed, drops the -I that pkg-config
    gives for it, and a stock header (curses.h) wins over the package's."""
    d = os.path.join(CACHE, "tools")
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    for name in os.listdir(tools):
        real = os.path.join(tools, name)
        if not name.startswith("m68k-elf-") or not os.access(real, os.X_OK):
            continue
        w = os.path.join(d, name)
        if name.rsplit("-", 1)[-1] in ("gcc", "g++", "c++", "cpp") or name.endswith(("-gcc-14.2.0",)):
            with open(w, "w") as f:
                f.write('#!/bin/sh\nexec "%s" "$@" -L"%s/usr/lib" -Wl,-rpath-link,"%s/usr/lib"\n'
                        % (real, sysroot, sysroot))
            os.chmod(w, 0o755)
        else:
            os.symlink(real, w)
    return d


def build(src, stem, repo, log=None):
    """Build stem from src against repo's packages and publish it into repo
    through src's gates; returns the published FMRI."""
    bad = problems()
    if bad:
        raise BuildError("; ".join(bad))
    comps = components(src)
    stem = resolve(src, stem)
    sysroot = build_sysroot(repo, comps, stem, log)
    env = dict(os.environ)
    # the recipes use GNU tar, sed and friends: illumos keeps them in /usr/gnu/bin
    gnu = "/usr/gnu/bin" + os.pathsep if os.path.isdir("/usr/gnu/bin") else ""
    tools = tool_wrappers(toolchain(), sysroot)
    # AMIX_REQUIRES_NO_BUILD: the requirements come from the sysroot (the
    # packages unpacked above); tools/amix-requires never builds them here
    env.update(AMIX_TOOLS=tools, AMIX_SYSROOT=sysroot, AMIX_REQUIRES_NO_BUILD="1",
               PATH=tools + os.pathsep + gnu + env.get("PATH", ""))
    d = comps[stem]["dir"]
    if needs_gcc27(d):
        if not toolchain27():
            raise BuildError("%s needs gcc 2.7.2.3 as well (developer/amix/gcc-27, "
                             "/opt/amix-cross/bin, or GCC27_TOOLS)" % stem)
        env["GCC27_TOOLS"] = toolchain27()
    _say(log, "== building %s" % stem)
    # build, then stage: not every component's stage target depends on its build
    _run([gmake(), "-C", d, "build"], log, env=env)
    _run([gmake(), "-C", d, "stage"], log, env=env)
    fd, p5m = tempfile.mkstemp(suffix=".p5m")
    os.close(fd)
    try:
        _run([sys.executable, os.path.join(src, "tools", "amix-p5m"), d, "-o", p5m], log, env=env)
        out = []
        _run([sys.executable, os.path.join(src, "tools", "amix-publish"), "-s", repo,
              "-d", os.path.join(d, "build", "stage"), "-d", d, p5m],
             lambda s: (out.append(s), _say(log, s)), env=env)
    finally:
        os.unlink(p5m)
    fmri = [s for s in out if s.startswith("pkg://")]
    if not fmri:
        _say(log, "%s: the same content as published: nothing new" % stem)
        return None
    _say(log, "published %s" % fmri[0])
    return fmri[0]


def build_many(src_spec, stems, bundled=None, log=None):
    """The CLI's and the window's way in: source, local repository, builds."""
    src = source_dir(src_spec, log)
    stems = [resolve(src, s) for s in stems]
    repo = ensure_local_repo(bundled or bundled_repo(), src, log)
    done = []
    for s in stems:
        done.append((s, build(src, s, repo, log)))
    return repo, done


def main_cli(src_spec, stems, list_only=False):
    def log(s):
        print(s, flush=True)
    try:
        src = source_dir(src_spec, log)
        if list_only:
            bundled = plan.default_repo()
            repo = LOCAL_REPO if os.path.isfile(os.path.join(LOCAL_REPO, "pkg5.repository")) else bundled
            if not repo:
                print("no AMIX package repository here (desktop/amix-installer/repository, "
                      "or AMIX_REPO): every component counts as new")
            for stem, rv, sv, rev, state in compare(src, repo):
                if state != "same":
                    print("%-8s %-36s %-24s source %s (%s)" % (state, stem, rv or "-", sv, rev))
            return 0
        stems = [resolve(src, s) for s in stems]       # short names; host components refused
        bad = problems()
        if bad:
            for b in bad:
                print("amix-installer: " + b, file=sys.stderr)
            return 1
        repo, done = build_many(src, stems, log=log)
        print("local repository: %s" % repo)
        return 0
    except BuildError as e:
        print("amix-installer: %s" % e, file=sys.stderr)
        return 1
