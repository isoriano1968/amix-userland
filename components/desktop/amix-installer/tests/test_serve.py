"""Serve Packages (amix_installer/serve.py): the repository read-only over
HTTP and the answer to the AMIX Software Center's Find on Network."""
import os
import socket
import sys
import tempfile
import unittest
import urllib.error
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from amix_installer import serve  # noqa: E402


def small_repo(d):
    cat = os.path.join(d, "publisher", "amix", "catalog")
    os.makedirs(cat)
    for stem in ("desktop%2Fcde", "x11%2Fx11r6.3"):
        os.makedirs(os.path.join(d, "publisher", "amix", "pkg", stem))
    with open(os.path.join(cat, "catalog.attrs"), "w") as f:
        f.write('{"parts": {}}')
    with open(os.path.join(d, "pkg5.repository"), "w") as f:
        f.write("[publisher]\nprefix = amix\n")
    with open(os.path.join(os.path.dirname(d), "secret"), "w") as f:
        f.write("not served")


class Serve(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = os.path.join(self.tmp.name, "repo")
        os.makedirs(self.repo)
        small_repo(self.repo)
        self.log = []
        self.srv = serve.RepoServer(self.repo, port=0, local=True, discovery_port=0,
                                    log=lambda *a: self.log.append(a)).start()

    def tearDown(self):
        self.srv.stop()
        self.tmp.cleanup()

    def get(self, path):
        return urllib.request.urlopen("http://127.0.0.1:%d%s" % (self.srv.port, path), timeout=5).read()

    def test_repo_files(self):
        self.assertEqual(self.get("/repo/publisher/amix/catalog/catalog.attrs"), b'{"parts": {}}')
        self.assertTrue(any(e[2] == "publisher/amix/catalog/catalog.attrs" and e[3] == 200 for e in self.log))
        self.assertEqual(self.srv.requests, 1)

    def test_nothing_outside(self):
        for p in ("/repo/../secret", "/repo/%2e%2e/secret", "/secret", "/repo/nosuch"):
            with self.assertRaises(urllib.error.HTTPError, msg=p):
                self.get(p)

    def test_no_writes(self):
        req = urllib.request.Request("http://127.0.0.1:%d/repo/x" % self.srv.port, data=b"x", method="PUT")
        with self.assertRaises(urllib.error.HTTPError):
            urllib.request.urlopen(req, timeout=5)

    def test_discovery(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(3)
        s.sendto(b"AMIX-REPO?\n", ("127.0.0.1", self.srv.discovery_port))
        data, _ = s.recvfrom(512)
        s.close()
        self.assertTrue(data.startswith(b"AMIX-REPO 1 port=%d path=/repo packages=2 date=" % self.srv.port), data)
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(1)
        s.sendto(b"hello", ("127.0.0.1", self.srv.discovery_port))
        with self.assertRaises(socket.timeout):
            s.recvfrom(512)
        s.close()

    def test_urls(self):
        u = self.srv.urls()
        self.assertEqual(u, [("FS-UAE on this computer", "http://10.0.2.2:%d/repo" % self.srv.port)])
        self.assertEqual(serve.command(u[0][1]), "pkg set-publisher -O %s amix" % u[0][1])

    def test_port_in_use(self):
        with self.assertRaises(OSError):
            serve.RepoServer(self.repo, port=self.srv.port, local=True, discovery=False).start()


if __name__ == "__main__":
    unittest.main()
