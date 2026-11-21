"""selfupdate.py: the check (offline, nothing newer, newer), the settings,
and the update itself (a sha256 mismatch, a cancel, a failure, success),
with a fake network and a fake package manager."""
import hashlib
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from amix_installer import selfupdate  # noqa: E402

DEB = b"!<arch>\nnot really a deb\n"
API_DEB = selfupdate.API % selfupdate.RELEASES["deb"]
API_OI = selfupdate.API % selfupdate.RELEASES["oi"]


def release(version, deb=DEB, sums=None):
    name = "amix-installer_%s-1_all.deb" % version
    return {"tag_name": "v" + version, "body": "## What is new\n- **self-update**",
            "html_url": "https://example.invalid/releases/v" + version,
            "assets": [{"name": name, "browser_download_url": "https://example.invalid/" + name},
                       {"name": "amix-gcc-14_14.2.0-2_amd64.deb", "browser_download_url": "x"},
                       {"name": "SHA256SUMS", "browser_download_url": "https://example.invalid/SUMS"}]}


class Net(object):
    """urlopen: url -> bytes; anything else is offline."""

    def __init__(self, pages):
        self.pages = pages

    def __call__(self, req, timeout=None):
        url = req.full_url if hasattr(req, "full_url") else req
        if url not in self.pages:
            raise OSError("offline")
        return io.BytesIO(self.pages[url])


def deb_net(version, deb=DEB, sum_of=DEB):
    rel = release(version)
    name = rel["assets"][0]["name"]
    return Net({API_DEB: json.dumps(rel).encode(),
                "https://example.invalid/" + name: deb,
                "https://example.invalid/SUMS": ("%s  %s\n" % (hashlib.sha256(sum_of).hexdigest(), name)).encode()})


def runner(rc=0, out="", pkg_list=None):
    """subprocess.run: pkg list -nH answers pkg_list; everything else rc/out."""
    calls = []

    def run(argv, **kw):
        calls.append(argv)
        if argv[:3] == ["pkg", "list", "-nH"]:
            if pkg_list is None:
                return subprocess.CompletedProcess(argv, 1, "", "")
            return subprocess.CompletedProcess(argv, 0, "%s %s ---\n" % (selfupdate.FMRI, pkg_list), "")
        return subprocess.CompletedProcess(argv, rc, out, "")
    run.calls = calls
    return run


class Check(unittest.TestCase):
    def test_offline_says_nothing(self):
        self.assertIsNone(selfupdate.check("deb", "0.9.0", opener=Net({})))
        self.assertIsNone(selfupdate.check("oi", "0.9.0", opener=Net({}), run=runner()))

    def test_nothing_newer(self):
        self.assertIsNone(selfupdate.check("deb", "0.9.0", opener=deb_net("0.9.0")))
        self.assertIsNone(selfupdate.check("deb", "0.9.1", opener=deb_net("0.9.0")))

    def test_newer_deb(self):
        u = selfupdate.check("deb", "0.9.0", opener=deb_net("0.10.0"))
        self.assertEqual((u.version, u.kind), ("0.10.0", "deb"))
        self.assertEqual(u.deb[0], "amix-installer_0.10.0-1_all.deb")
        self.assertIn("self-update", u.notes)

    def test_newer_oi_from_the_catalog(self):
        u = selfupdate.check("oi", "0.9.0", opener=Net({}), run=runner(pkg_list="0.9.1-2026.0.0.0"))
        self.assertEqual((u.version, u.kind), ("0.9.1", "oi"))
        self.assertIsNone(selfupdate.check("oi", "0.9.1", opener=Net({}),
                                           run=runner(pkg_list="0.9.1-2026.0.0.0")))

    def test_oi_falls_back_to_the_release(self):
        net = Net({API_OI: json.dumps(release("0.9.2")).encode()})
        u = selfupdate.check("oi", "0.9.0", opener=net, run=runner(pkg_list=None))
        self.assertEqual(u.version, "0.9.2")

    def test_source_tree_checks_nothing(self):
        self.assertIsNone(selfupdate.check(None, "0.9.0", opener=deb_net("1.0.0"),
                                           run=lambda *a, **k: (_ for _ in ()).throw(OSError())))

    def test_vkey(self):
        self.assertGreater(selfupdate.vkey("0.10.0"), selfupdate.vkey("0.9.9"))
        self.assertEqual(selfupdate.vkey("0.9.0-2026.0.0.4"), selfupdate.vkey("0.9.0"))


class Settings(unittest.TestCase):
    def test_later_hides_until_the_next(self):
        with tempfile.TemporaryDirectory() as t:
            old = selfupdate.SETTINGS
            selfupdate.SETTINGS = os.path.join(t, "u.json")
            try:
                self.assertEqual(selfupdate.settings(), {"check": True, "later": None})
                u = selfupdate.Update("0.9.1", "deb")
                self.assertTrue(selfupdate.wanted(u))
                selfupdate.save_settings({"check": True, "later": "0.9.1"})
                self.assertFalse(selfupdate.wanted(u))
                self.assertTrue(selfupdate.wanted(selfupdate.Update("0.9.2", "deb")))
                selfupdate.save_settings({"check": False, "later": None})
                self.assertFalse(selfupdate.wanted(selfupdate.Update("0.9.2", "deb")))
            finally:
                selfupdate.SETTINGS = old


class Apply(unittest.TestCase):
    def test_deb_installs_the_checked_file(self):
        net = deb_net("0.10.0")
        u = selfupdate.check("deb", "0.9.0", opener=net)
        run = runner(rc=0)
        ok, text = selfupdate.apply(u, opener=net, run=run)
        self.assertTrue(ok, text)
        argv = run.calls[-1]
        self.assertEqual(argv[:3], ["pkexec", "apt-get", "install"])
        self.assertTrue(argv[-1].endswith("/amix-installer_0.10.0-1_all.deb"))

    def test_sha256_mismatch_installs_nothing(self):
        net = deb_net("0.10.0", deb=DEB + b"tampered")
        u = selfupdate.check("deb", "0.9.0", opener=net)
        run = runner(rc=0)
        ok, text = selfupdate.apply(u, opener=net, run=run)
        self.assertFalse(ok)
        self.assertIn("nothing installed", text)
        self.assertEqual(run.calls, [])

    def test_cancelled(self):
        net = deb_net("0.10.0")
        u = selfupdate.check("deb", "0.9.0", opener=net)
        ok, text = selfupdate.apply(u, opener=net, run=runner(rc=126))
        self.assertFalse(ok)
        self.assertIn("cancelled", text)

    def test_oi_failure_is_reported(self):
        u = selfupdate.Update("0.9.1", "oi")
        run = runner(rc=1, out="pkg: no updates available\n")
        ok, text = selfupdate.apply(u, run=run)
        self.assertFalse(ok)
        self.assertIn("no updates available", text)
        self.assertEqual(run.calls[-1], ["pfexec", "pkg", "update", selfupdate.FMRI])

    def test_offline_download(self):
        u = selfupdate.Update("0.10.0", "deb", deb=("amix-installer_0.10.0-1_all.deb", "https://x"),
                              sums="https://y")
        ok, text = selfupdate.apply(u, opener=Net({}), run=runner())
        self.assertFalse(ok)


if __name__ == "__main__":
    unittest.main()
