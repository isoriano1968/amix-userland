"""Every shell script that runs on AMIX is plain SVR4 Bourne sh: stock AMIX's
/sbin/sh has no "! cmd", no $(...), no arithmetic, no [[ ]], no local, no
"export VAR=value", no ${var%...}; its test has no -e.  The host's sh (dash,
bash) accepts all of them, so Scripts.sh_ok cannot see them: this lint does.
0.6.7's mkfs guard ("! fstyp ... || fail") stopped the user's install on
a fresh data disk, because AMIX ran "!" as a command."""
import os
import re
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from amix_installer import model, netinstall  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.join(HERE, "..", "..", "..", "..")

# (name, pattern) on a line with its quoted strings and comments taken out
NOT_BOURNE = [
    ("'! cmd' (negation)", re.compile(r"(^|[;&|({]|\bthen|\bdo|\belse|\bif|\bwhile|\buntil)\s*!\s")),
    ("$(...)", re.compile(r"\$\((?!\()")),
    ("$((...))", re.compile(r"\$\(\(")),
    ("[[ ]]", re.compile(r"\[\[")),
    ("local", re.compile(r"(^|[;&|]\s*)local\s")),
    ("function", re.compile(r"(^|[;&|]\s*)function\s")),
    ("export VAR=value", re.compile(r"(^|[;&|]\s*)export\s+\w+=")),
    ("${var%...} / ${var#...} / ${#var} / ${var/...}", re.compile(r"\$\{#|\$\{\w+(%|#|/)")),
    ("test -e", re.compile(r"\[\s+(!\s+)?-e\s")),
    ("==", re.compile(r"\[\s[^]]*\s==\s")),
    ("<<<", re.compile(r"<<<")),
    ("source", re.compile(r"(^|[;&|]\s*)source\s")),
    ("echo -n / -e", re.compile(r"(^|[;&|]\s*)echo\s+-[ne]\b")),
]


def strip(line):
    """The line without its comment and without what is inside single
    quotes (sed and awk programs); double quotes stay ($(...) works there)."""
    out, q = [], None
    for ch in line:
        if q == "'":
            if ch == "'":
                q = None
            continue
        if q is None and ch == "'":
            q = "'"
            continue
        if q is None and ch == "#" and (not out or out[-1] in " \t;"):
            break
        if ch == '"':
            q = None if q == '"' else '"'
        out.append(ch)
    return "".join(out)


def problems(text, name):
    found = []
    here = None                                   # inside a here-document
    for n, line in enumerate(text.splitlines(), 1):
        if here is not None:
            if line.strip() == here:
                here = None
            continue
        m = re.search(r"<<-?\s*'?(\w+)'?", line)
        body = strip(line)
        for what, rx in NOT_BOURNE:
            if rx.search(body):
                found.append("%s:%d: %s: %s" % (name, n, what, line.strip()))
        if m and "<<<" not in line:
            here = m.group(1)
    return found


def scripts():
    """Every script the installer sends to AMIX, for a spread of choices."""
    with tempfile.TemporaryDirectory() as d:
        from test_netinstall import fake_repo
        fake_repo(d, {p: ([], [1000]) for p in ["sysutils/amix-pkg", "library/zlib",
                                                 "library/amixc99", "library/libm99",
                                                 "sysutils/setclk", "gnu/gnu-tools",
                                                 "developer/vscode-agent", "desktop/motif",
                                                 "x11/x11r6.3", "desktop/cde"]})
        out = [("go", netinstall.go_script("10.0.2.2"))]
        now = time.gmtime(0)
        for sel in ({"src": "run"}, {"desk": "cde", "gfx": "picasso2"}, {"layout": "single"}):
            cfg = model.Config(sel=sel)
            cfg.on["vscode"] = True
            out.append(("steps %s" % sel, netinstall.steps_script(
                cfg, "10.0.2.2", data_disk=("/dev/rdsk/c1d0s1", 2047872, "/dev/rdsk/c1d0s1"),
                repo=d, kernel=(1500000, 1234), patch=True, now=now, agent_key="k" * 64)))
            out.append(("steps no data %s" % sel, netinstall.steps_script(
                cfg, "10.0.2.2", repo=d, now=now)))
        out.append(("agent-only", netinstall.agent_only_script("10.0.2.2", 8069, d, "k" * 64,
                                                               now=now)))
        out.append(("agent and motif", netinstall.agent_only_script(
            "10.0.2.2", 8069, d, "k" * 64, now=now, extra=["desktop/motif"])))
    return out


# scripts in packages and tests that run on AMIX itself
AMIX_FILES = ["tests/e2e/go2.sh",
              "../cde-dtlogin/files/startcde", "../cde-dtlogin/files/Xamix-dt",
              "../cde-dtlogin/files/dtlogin.rc", "../cde-dtlogin/files/dtconfig",
              "../../developer/vscode-agent/files/vscode-agent.rc"]


class Bourne(unittest.TestCase):
    def test_the_lint_sees_it(self):
        self.assertTrue(problems("  ! /usr/lib/fs/ufs/fstyp $D || fail x\n", "t"))
        self.assertTrue(problems("a=$(uname)\n", "t"))
        self.assertTrue(problems("export X=1\n", "t"))
        self.assertFalse(problems("[ ! -f /x ] || exit 1\n", "t"))
        self.assertFalse(problems("echo '! $(x)'\n", "t"))
        self.assertFalse(problems("cat <<E\n! $(x)\nE\n", "t"))

    def test_generated_scripts(self):
        bad = []
        for name, text in scripts():
            bad += problems(text, name)
        self.assertEqual(bad, [], "\n".join(bad))

    def test_amix_files(self):
        bad = []
        app = os.path.join(HERE, "..")
        for f in AMIX_FILES:
            p = os.path.normpath(os.path.join(app, f))
            if os.path.exists(p):
                with open(p) as fh:
                    bad += problems(fh.read(), f)
        self.assertEqual(bad, [], "\n".join(bad))


if __name__ == "__main__":
    unittest.main()
