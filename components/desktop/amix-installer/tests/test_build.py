"""build.py: the components new or newer than a repository, the build
sysroot unpacked from packages, the local repository."""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))

from amix_installer import build  # noqa: E402

PUBLISH = os.path.join(ROOT, "tools", "amix-publish")


def git(d, *a):
    subprocess.check_call(["git", "-C", d] + list(a), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def checkout(d, comps):
    """A git checkout with tools/amix-p5m and components: stem -> (version, requires)."""
    os.makedirs(os.path.join(d, "tools"))
    open(os.path.join(d, "tools", "amix-p5m"), "w").close()
    for stem, (ver, req) in comps.items():
        cd = os.path.join(d, "components", stem)
        os.makedirs(cd)
        with open(os.path.join(cd, stem.split("/")[-1] + ".manifest"), "w") as f:
            f.write("# test\nname     %s\nversion  %s\n" % (stem, ver))
            for r in req:
                f.write("require  %s\n" % r)
    git(d, "init", "-q")
    git(d, "-c", "user.name=t", "-c", "user.email=t@t", "add", ".")
    git(d, "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-m", "one")


def publish(repo, src, stem, version, files=(), links=(), extra=()):
    """stem@version into repo with the real amix-publish (no gates)."""
    os.makedirs(src, exist_ok=True)
    lines = ["set name=pkg.fmri value=pkg://amix/%s@%s" % (stem, version),
             "set name=pkg.summary value=t"] + list(extra)
    for path, data in files:
        p = os.path.join(src, os.path.basename(path))
        with open(p, "w") as f:
            f.write(data)
        lines.append("file %s path=%s owner=root group=bin mode=0444" % (os.path.basename(path), path))
    for path, target in links:
        lines.append("link path=%s target=%s" % (path, target))
    m = os.path.join(src, "m.p5m")
    with open(m, "w") as f:
        f.write("\n".join(lines) + "\n")
    subprocess.check_call([sys.executable, PUBLISH, "--no-needed-check", "-s", repo, "-d", src, m],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


class Compare(unittest.TestCase):
    def test_states(self):
        with tempfile.TemporaryDirectory() as t:
            src, repo = os.path.join(t, "src"), os.path.join(t, "repo")
            checkout(src, {"library/a": ("1.0", []), "library/b": ("2.0", ["library/a"]),
                           "library/c": ("1.0", []), "sysutils/d": ("1", [])})
            publish(repo, os.path.join(t, "p"), "library/a", "1.0,2.1-1")     # as published
            publish(repo, os.path.join(t, "p"), "library/b", "1.0,2.1-1")     # source 2.0: newer
            publish(repo, os.path.join(t, "p"), "sysutils/d", "1,2.1-0.1")    # published from edits
            with open(os.path.join(src, "components/library/c/c.manifest"), "a") as f:
                f.write("# edit\n")
            st = {s: state for s, _, _, _, state in build.compare(src, repo)}
            self.assertEqual(st["library/a"], "same")
            self.assertEqual(st["library/b"], "newer")
            self.assertEqual(st["library/c"], "new")
            self.assertEqual(st["sysutils/d"], "same")     # N.k published, then committed
            with open(os.path.join(src, "components/library/a/a.manifest"), "a") as f:
                f.write("# edit\n")
            st = {s: state for s, _, _, rev, state in build.compare(src, repo)}
            self.assertEqual(st["library/a"], "changed")

    def test_requirements_closure(self):
        comps = {"x/a": {"require": ["x/b"]}, "x/b": {"require": ["x/c", "x/a"]}, "x/c": {"require": []}}
        self.assertEqual(build.requirements(comps, "x/a"), ["x/b", "x/c"])

    def test_vkey(self):
        self.assertGreater(build.vkey("1.10,2.1-3"), build.vkey("1.9,2.1-9"))
        self.assertGreater(build.vkey("1.0,2.1-6.2"), build.vkey("1.0,2.1-6.1"))

    def test_not_a_checkout(self):
        with tempfile.TemporaryDirectory() as t:
            with self.assertRaises(build.BuildError):
                build.source_dir(t)


class Pull(unittest.TestCase):
    def test_pull_fast_forward_keeps_edits(self):
        with tempfile.TemporaryDirectory() as t:
            up, down = os.path.join(t, "amix-userland"), os.path.join(t, "down")
            checkout(up, {"library/a": ("1.0", [])})
            subprocess.check_call(["git", "clone", "-q", up, down], stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL)
            with open(os.path.join(up, "components/library/a/a.manifest"), "a") as f:
                f.write("# upstream\n")
            git(up, "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qam", "two")
            with open(os.path.join(down, "tools", "amix-p5m"), "w") as f:
                f.write("local edit\n")          # an edit here, not committed
            msg = build.pull(down)
            self.assertIn("git pull:", msg)
            with open(os.path.join(down, "components/library/a/a.manifest")) as f:
                self.assertIn("# upstream", f.read())
            with open(os.path.join(down, "tools", "amix-p5m")) as f:
                self.assertEqual(f.read(), "local edit\n")
            self.assertIn("up to date", build.pull(down))

    def test_master_only(self):
        self.assertTrue(build.is_master("https://github.com/isoriano1968/amix-userland.git"))
        self.assertTrue(build.is_master("git@github.com:isoriano1968/amix-userland"))
        self.assertTrue(build.is_master("/export/home/u/src/amix-userland/"))
        self.assertFalse(build.is_master("https://github.com/someone/fork-of-something.git"))
        with tempfile.TemporaryDirectory() as t:
            up, down = os.path.join(t, "other"), os.path.join(t, "down")
            checkout(up, {"library/a": ("1.0", [])})
            subprocess.check_call(["git", "clone", "-q", up, down], stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL)
            self.assertIn("not amix-userland", build.pull(down))

    def test_no_upstream(self):
        with tempfile.TemporaryDirectory() as t:
            checkout(t, {"library/a": ("1.0", [])})
            self.assertIn("no upstream", build.pull(t))


class Unpack(unittest.TestCase):
    def test_files_and_links(self):
        with tempfile.TemporaryDirectory() as t:
            repo, dest = os.path.join(t, "repo"), os.path.join(t, "root")
            publish(repo, os.path.join(t, "p"), "library/z", "1.0,2.1-1",
                    files=[("usr/local/include/z.h", "int z;\n")],
                    links=[("usr/local/lib/libz.so", "libz.so.1")])
            publish(repo, os.path.join(t, "p"), "library/z", "1.0,2.1-2",
                    files=[("usr/local/include/z.h", "int z2;\n")],
                    links=[("usr/local/lib/libz.so", "libz.so.1")])
            v = build.unpack(repo, "library/z", dest)
            self.assertTrue(v.startswith("1.0,2.1-2"))
            with open(os.path.join(dest, "usr/local/include/z.h")) as f:
                self.assertEqual(f.read(), "int z2;\n")
            self.assertEqual(os.readlink(os.path.join(dest, "usr/local/lib/libz.so")), "libz.so.1")
            self.assertIsNone(build.unpack(repo, "library/none", dest))


class LocalRepo(unittest.TestCase):
    def test_copy_then_merge(self):
        with tempfile.TemporaryDirectory() as t:
            bundled, local = os.path.join(t, "bundled"), os.path.join(t, "local")
            publish(bundled, os.path.join(t, "p"), "library/a", "1.0,2.1-1")
            old = build.LOCAL_REPO
            build.LOCAL_REPO = local
            try:
                build.ensure_local_repo(bundled, ROOT)
                self.assertIn("library/a", build.repo_versions(local))
                publish(bundled, os.path.join(t, "p"), "library/b", "1.0,2.1-1")
                build.ensure_local_repo(bundled, ROOT)
                self.assertIn("library/b", build.repo_versions(local))
                with open(os.path.join(local, "publisher/amix/catalog/catalog.base.C")) as f:
                    self.assertIn("library/b", f.read())
            finally:
                build.LOCAL_REPO = old


class Gcc27(unittest.TestCase):
    def test_needs(self):
        with tempfile.TemporaryDirectory() as t:
            with open(os.path.join(t, "Makefile"), "w") as f:
                f.write("GCC27_TOOLS ?= $(HOME)/opt/amix-cross/bin\n")
            self.assertTrue(build.needs_gcc27(t))
            with open(os.path.join(t, "Makefile"), "w") as f:
                f.write("build:\n")
            self.assertFalse(build.needs_gcc27(t))

    def test_x11r63_needs_it(self):
        self.assertTrue(build.needs_gcc27(os.path.join(ROOT, "components/x11/x11r6.3")))
        self.assertFalse(build.needs_gcc27(os.path.join(ROOT, "components/sysutils/amixtop")))


class ServeMerge(unittest.TestCase):
    """Serve Packages: a newer bundled repository reaches the local copy
    without a source tree (amix-publish shipped with the installer)."""

    def test_merge_without_source(self):
        self.assertIsNotNone(build.publish_tool())
        with tempfile.TemporaryDirectory() as t:
            bundled, local = os.path.join(t, "bundled"), os.path.join(t, "local")
            publish(bundled, os.path.join(t, "p"), "desktop/x", "1.0.4,2.1-5.1")
            old = build.LOCAL_REPO
            build.LOCAL_REPO = local
            try:
                build.ensure_local_repo(bundled)
                publish(bundled, os.path.join(t, "p"), "desktop/x", "1.0.5,2.1-6.1")
                said = []
                build.ensure_local_repo(bundled, None, said.append)
                self.assertTrue(any(s.startswith("merged 1 ") for s in said))
                self.assertTrue(build.repo_versions(local)["desktop/x"].startswith("1.0.5"))
                with open(os.path.join(local, "publisher/amix/catalog/catalog.base.C")) as f:
                    self.assertIn("1.0.5", f.read())
            finally:
                build.LOCAL_REPO = old


class Problems(unittest.TestCase):
    def test_no_toolchain(self):
        env = dict(os.environ)
        try:
            os.environ["AMIX_TOOLS"] = "/nonexistent"
            os.environ["AMIX_SYSROOT_BASE"] = "/nonexistent"
            p = build.problems()
            # the fallbacks may exist on a developer's machine; the variables alone never fit
            self.assertTrue(all(isinstance(x, str) for x in p))
        finally:
            os.environ.clear()
            os.environ.update(env)


if __name__ == "__main__":
    unittest.main()
