#!/usr/local/bin/python3
"""pycheck.py -- a quick health check of Python and its standard library on AMIX.

Each check runs on its own and reports one line:
  OK       it works (with the time it took)
  FAIL     the module loaded but gave a wrong answer or raised
  MISSING  a module, usually a C extension, is not in this build

Usage: python3 pycheck.py [-v]      (-v prints the traceback of each FAIL)
Exit status: 0 when nothing FAILed (MISSING does not count), 1 otherwise.
"""
import sys, time, traceback

VERBOSE = "-v" in sys.argv
results = []
checks = []


def check(fn):
    checks.append(fn)
    return fn


def run(fn):
    name = fn.__name__[2:]
    t0 = time.time()
    try:
        note = fn() or ""
        status = "OK"
    except ImportError as e:
        status, note = "MISSING", str(e)
    except Exception as e:
        status, note = "FAIL", "%s: %s" % (type(e).__name__, e)
        if VERBOSE:
            traceback.print_exc()
    ms = (time.time() - t0) * 1000
    results.append(status)
    print("%-7s %-14s %7.0f ms  %s" % (status, name, ms, note))
    sys.stdout.flush()


# ---- the language and the core ----

@check
def c_core():
    assert 2 ** 100 == 1267650600228229401496703205376
    assert divmod(-7, 2) == (-4, 1)
    assert sorted({3: 1, 1: 2, 2: 3}) == [1, 2, 3]
    assert [x * x for x in range(5) if x % 2] == [1, 9]
    assert f"{255:#06x} {3.14159:.2f} {'ab':>4}" == "0x00ff 3.14   ab"
    s = "Grüße, 日本"
    assert s.encode("utf-8").decode("utf-8") == s
    assert len(s.encode("utf-8")) == 15
    return "%s, %s-endian" % (sys.version.split()[0], sys.byteorder)


@check
def c_exceptions():
    class E(Exception):
        pass
    try:
        try:
            raise E("inner")
        except E as e:
            raise ValueError("outer") from e
    except ValueError as v:
        assert isinstance(v.__cause__, E)
    with open(__file__) as f:
        assert f.readline().startswith("#!")


@check
def c_generators():
    def gen(n):
        for i in range(n):
            got = yield i
            if got:
                yield got

    g = gen(3)
    assert next(g) == 0 and g.send("x") == "x" and list(g) == [1, 2]

    async def co():
        return 42
    c = co()
    try:
        c.send(None)
    except StopIteration as s:
        assert s.value == 42


@check
def c_math():
    import math
    assert math.isclose(math.sin(math.pi / 6), 0.5, rel_tol=1e-12)
    assert math.isclose(math.exp(math.log(10.0)), 10.0, rel_tol=1e-12)
    assert math.isclose(math.sqrt(2.0) ** 2, 2.0, rel_tol=1e-12)
    assert math.factorial(20) == 2432902008176640000
    assert math.gcd(462, 1071) == 21
    assert float("1e308") * 10 == math.inf and math.isnan(math.inf - math.inf)
    assert repr(0.1 + 0.2) == "0.30000000000000004"
    assert round(2.675, 2) == 2.67


@check
def c_decimal():
    from decimal import Decimal, getcontext
    from fractions import Fraction
    getcontext().prec = 30
    assert str(Decimal(1) / Decimal(7)) == "0.142857142857142857142857142857"
    assert Fraction(1, 3) + Fraction(1, 6) == Fraction(1, 2)
    import decimal
    return "C" if hasattr(decimal, "__libmpdec_version__") else "pure Python"


@check
def c_statistics():
    import statistics
    d = [2, 4, 4, 4, 5, 5, 7, 9]
    assert statistics.mean(d) == 5 and statistics.median(d) == 4.5
    assert statistics.pstdev(d) == 2.0


# ---- data structures and text ----

@check
def c_collections():
    from collections import Counter, OrderedDict, defaultdict, deque, namedtuple
    assert Counter("abracadabra").most_common(1) == [("a", 5)]
    d = defaultdict(list); d["k"].append(1); assert d == {"k": [1]}
    q = deque(range(5), maxlen=3); q.append(9); assert list(q) == [3, 4, 9]
    P = namedtuple("P", "x y"); assert P(1, 2).y == 2
    o = OrderedDict(a=1, b=2); o.move_to_end("a"); assert list(o) == ["b", "a"]


@check
def c_itertools():
    import itertools, functools, operator
    assert list(itertools.permutations("abc", 2))[:2] == [("a", "b"), ("a", "c")]
    assert list(itertools.accumulate([1, 2, 3])) == [1, 3, 6]
    assert functools.reduce(operator.mul, range(1, 6)) == 120

    @functools.lru_cache(None)
    def fib(n):
        return n if n < 2 else fib(n - 1) + fib(n - 2)
    assert fib(80) == 23416728348467685


@check
def c_heapq_bisect():
    import heapq, bisect
    h = [5, 1, 4, 2]; heapq.heapify(h)
    assert [heapq.heappop(h) for _ in range(4)] == [1, 2, 4, 5]
    a = [1, 3, 5]; bisect.insort(a, 4); assert a == [1, 3, 4, 5]


@check
def c_array():
    import array
    a = array.array("i", [1, 2, 3]); a.byteswap()
    assert a.tobytes()[:4] == (1).to_bytes(4, "little" if sys.byteorder == "big" else "big")


@check
def c_re():
    import re
    m = re.match(r"(?P<user>\w+)@(?P<host>[\w.]+)", "root@amiga3000ux")
    assert m.group("host") == "amiga3000ux"
    assert re.sub(r"(\d+)", lambda m: str(int(m.group()) * 2), "a1b22") == "a2b44"
    assert re.findall(r"\bfo\w*", "foo bar fob", re.I) == ["foo", "fob"]


@check
def c_string_tools():
    import string, textwrap, difflib
    assert string.Template("$x-$y").substitute(x=1, y=2) == "1-2"
    assert textwrap.wrap("aaa bbb ccc", 7) == ["aaa bbb", "ccc"]
    assert difflib.SequenceMatcher(None, "abcd", "abxd").ratio() == 0.75


@check
def c_unicodedata():
    import unicodedata
    assert unicodedata.name("ß") == "LATIN SMALL LETTER SHARP S"
    assert unicodedata.normalize("NFC", "ü") == "ü"


@check
def c_codecs():
    s = "Ärger"
    for enc in ("latin-1", "cp1252", "utf-16", "utf-32", "ascii"):
        try:
            assert s.encode(enc).decode(enc) == s
        except UnicodeEncodeError:
            assert enc == "ascii"
    import base64
    assert base64.b64encode(b"AMIX") == b"QU1JWA=="


# ---- serialisation, hashing, compression ----

@check
def c_json():
    import json
    obj = {"name": "AMIX", "cpu": 68060, "mhz": 80.0, "ok": True, "x": None, "l": [1, "ü"]}
    assert json.loads(json.dumps(obj)) == obj
    assert json.dumps({"b": 1, "a": 2}, sort_keys=True) == '{"a": 2, "b": 1}'


@check
def c_struct():
    import struct
    assert struct.pack(">IH", 0x12345678, 0xABCD) == b"\x12\x34\x56\x78\xab\xcd"
    assert struct.pack("=I", 1) == (1).to_bytes(4, sys.byteorder)
    assert struct.unpack("<d", struct.pack("<d", 1.5)) == (1.5,)
    return "%d-bit pointers" % (struct.calcsize("P") * 8)


@check
def c_pickle():
    import pickle, copy
    obj = {"t": (1, 2), "s": {3}, "f": 1.5, "b": b"x"}
    for proto in range(pickle.HIGHEST_PROTOCOL + 1):
        assert pickle.loads(pickle.dumps(obj, proto)) == obj
    deep = copy.deepcopy([[1], [2]]); assert deep == [[1], [2]]
    import _pickle  # noqa: F401  -- the C accelerator; pure Python works without it


@check
def c_hashlib():
    import hashlib, hmac
    want = {
        "md5": "900150983cd24fb0d6963f7d28e17f72",
        "sha1": "a9993e364706816aba3e25717850c26c9cd0d89d",
        "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "sha3_256": "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532",
        "blake2s": "508c5e8c327c14e2e1a72ba34eeb452f37458b209ed63a294d999b4c86675982",
    }
    for name, hx in want.items():
        assert hashlib.new(name, b"abc").hexdigest() == hx, name
    assert hmac.new(b"key", b"msg", "sha256").hexdigest().startswith("2d93cbc1")


@check
def c_zlib():
    import zlib, gzip, io, binascii
    data = b"AMIX on the Amiga 3000 " * 200
    z = zlib.compress(data, 9)
    assert zlib.decompress(z) == data and len(z) < 100
    assert binascii.crc32(b"123456789") == 0xCBF43926
    buf = io.BytesIO()
    with gzip.GzipFile(fileobj=buf, mode="wb") as f:
        f.write(data)
    assert gzip.decompress(buf.getvalue()) == data
    return "zlib %s, ratio %d:1" % (zlib.ZLIB_VERSION, len(data) // len(z))


@check
def c_bz2_lzma():
    import bz2
    assert bz2.decompress(bz2.compress(b"x" * 1000)) == b"x" * 1000
    import lzma
    assert lzma.decompress(lzma.compress(b"y" * 1000)) == b"y" * 1000


@check
def c_csv():
    import csv, io
    rows = [["name", "value"], ["a,b", "1"], ['q"t', "2"]]
    buf = io.StringIO(); csv.writer(buf).writerows(rows)
    assert list(csv.reader(io.StringIO(buf.getvalue()))) == rows


@check
def c_sqlite3():
    import sqlite3
    db = sqlite3.connect(":memory:")
    db.execute("create table t (k text, v int)")
    db.executemany("insert into t values (?, ?)", [("a", 1), ("b", 2)])
    assert db.execute("select sum(v) from t").fetchone() == (3,)


# ---- randomness ----

@check
def c_random():
    import random
    random.seed(1968)
    a = [random.random() for _ in range(3)]
    random.seed(1968)
    assert a == [random.random() for _ in range(3)]
    assert sorted(random.sample(range(10), 10)) == list(range(10))


@check
def c_urandom():
    import os, secrets
    a, b = os.urandom(32), os.urandom(32)
    assert len(a) == 32 and a != b
    assert len(secrets.token_hex(16)) == 32
    t0 = time.time(); os.urandom(65536); dt = time.time() - t0
    return "/dev/urandom 64 KB in %.2f s" % dt


# ---- the system ----

@check
def c_os():
    import os, platform
    assert os.getcwd() and os.path.isdir("/tmp")
    assert os.getpid() > 0 and os.environ.get("PATH")
    u = os.uname()
    return "%s %s %s, uid %d" % (u.sysname, u.release, u.machine, os.getuid())


@check
def c_files():
    import os, tempfile, shutil, pathlib, glob
    d = tempfile.mkdtemp(prefix="pycheck")
    try:
        p = pathlib.Path(d) / "a.txt"
        p.write_text("eins\nzwei\n", encoding="utf-8")
        assert p.read_text(encoding="utf-8").splitlines() == ["eins", "zwei"]
        shutil.copy(p, os.path.join(d, "b.txt"))
        os.rename(os.path.join(d, "b.txt"), os.path.join(d, "c.txt"))
        assert sorted(os.path.basename(x) for x in glob.glob(d + "/*.txt")) == ["a.txt", "c.txt"]
        st = os.stat(p); assert st.st_size == 10
        with open(p, "rb+") as f:
            f.seek(5); f.write(b"Z"); f.seek(0); assert f.read() == b"eins\nZwei\n"
    finally:
        shutil.rmtree(d)
    assert not os.path.exists(d)


@check
def c_time():
    import datetime, calendar
    t0 = time.time(); time.sleep(0.2); dt = time.time() - t0
    assert 0.15 < dt < 2.0, dt
    d = datetime.date(2026, 10, 28)
    assert d.strftime("%A") == "Wednesday" and d + datetime.timedelta(days=4) == datetime.date(2026, 11, 1)
    assert calendar.isleap(2028)
    return time.strftime("%Y-%m-%d %H:%M:%S")


@check
def c_pwd():
    import pwd, grp
    assert pwd.getpwnam("root").pw_uid == 0
    assert grp.getgrgid(0)


@check
def c_signal():
    import signal
    hit = []
    signal.signal(signal.SIGALRM, lambda s, f: hit.append(s))
    signal.alarm(1)
    t0 = time.time()
    while not hit and time.time() - t0 < 5:
        time.sleep(0.05)
    signal.signal(signal.SIGALRM, signal.SIG_DFL)
    assert hit == [signal.SIGALRM]


@check
def c_select_pipe():
    import os, select
    r, w = os.pipe()
    os.write(w, b"ping")
    rl, _, _ = select.select([r], [], [], 1.0)
    assert rl == [r] and os.read(r, 4) == b"ping"
    os.close(r); os.close(w)


@check
def c_fork():
    import os
    pid = os.fork()
    if pid == 0:
        os._exit(7)
    _, status = os.waitpid(pid, 0)
    assert os.WIFEXITED(status) and os.WEXITSTATUS(status) == 7


@check
def c_subprocess():
    import subprocess
    out = subprocess.run(["/usr/bin/echo", "hello"], capture_output=True, text=True)
    assert out.returncode == 0 and out.stdout.strip() == "hello"


@check
def c_threading():
    import threading, queue
    q = queue.Queue(); lock = threading.Lock(); total = [0]

    def worker(n):
        for i in range(n):
            with lock:
                total[0] += 1
        q.put(n)
    ts = [threading.Thread(target=worker, args=(500,)) for _ in range(4)]
    for t in ts:
        t.start()
    for t in ts:
        t.join(10)
    assert total[0] == 2000 and q.qsize() == 4


@check
def c_socket():
    import socket
    a, b = socket.socketpair()
    a.sendall(b"hi"); assert b.recv(2) == b"hi"
    return socket.gethostname()


@check
def c_misc_stdlib():
    import argparse, logging, io, dataclasses, enum, typing, contextlib, weakref, gc
    ap = argparse.ArgumentParser(); ap.add_argument("-n", type=int, default=3)
    assert ap.parse_args(["-n", "5"]).n == 5
    s = io.StringIO(); h = logging.StreamHandler(s); lg = logging.getLogger("pycheck")
    lg.addHandler(h); lg.warning("w"); lg.removeHandler(h); assert s.getvalue() == "w\n"

    @dataclasses.dataclass(order=True)
    class P:
        x: int
        y: int = 0
    assert sorted([P(2), P(1, 5)])[0] == P(1, 5)

    class C(enum.Enum):
        A = 1
    assert C(1) is C.A

    class O:
        pass
    o = O(); r = weakref.ref(o); del o; gc.collect(); assert r() is None


def main():
    print("pycheck on %s -- Python %s" % (sys.platform, sys.version.replace("\n", " ")))
    print("%s" % sys.executable)
    print()
    t0 = time.time()
    for fn in checks:
        run(fn)
    n = {k: results.count(k) for k in ("OK", "FAIL", "MISSING")}
    print()
    print("%d checks: %d OK, %d FAIL, %d MISSING in %.1f s"
          % (len(results), n["OK"], n["FAIL"], n["MISSING"], time.time() - t0))
    return 1 if n["FAIL"] else 0


if __name__ == "__main__":
    sys.exit(main())
