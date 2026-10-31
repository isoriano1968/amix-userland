"""Bounded tests of the extra statically linked AMIX Python modules."""
import datetime
import gzip
import hashlib
import io
import math
import os
import select
import struct
import zipfile
import zlib

data = bytes(range(256)) * 1024
assert zlib.decompress(zlib.compress(data)) == data
assert gzip.decompress(gzip.compress(data, mtime=0)) == data
archive = io.BytesIO()
with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
    output.writestr("roundtrip.bin", data)
with zipfile.ZipFile(io.BytesIO(archive.getvalue())) as source:
    assert source.read("roundtrip.bin") == data
assert hashlib.sha256(b"abc").hexdigest() == (
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
assert hashlib.md5(b"abc").hexdigest() == "900150983cd24fb0d6963f7d28e17f72"
assert hashlib.sha1(b"abc").hexdigest() == "a9993e364706816aba3e25717850c26c9cd0d89d"
assert hashlib.sha3_256(b"abc").hexdigest() == (
    "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532")
assert struct.unpack(">IQ", struct.pack(">IQ", 0x12345678, 0x123456789ABCDEF0)) == (
    0x12345678, 0x123456789ABCDEF0)
assert datetime.datetime(2026, 10, 28).isoformat() == "2026-10-28T00:00:00"
for n in range(-100, 101):
    assert math.exp2(n) == math.ldexp(1.0, n)
    assert math.log2(math.ldexp(1.0, n)) == n
assert math.expm1(1e-20) == 1e-20 and math.log1p(1e-20) == 1e-20
assert math.nextafter(0.0, -1.0) < 0.0
assert math.nextafter(1.0, 2.0) == float.fromhex("0x1.0000000000001p+0")
read, write = os.pipe()
try:
    os.write(write, b"pipe")
    assert select.select([read], [], [], 1)[0] == [read]
    assert os.read(read, 4) == b"pipe"
finally:
    os.close(read)
    os.close(write)
name = "AMIX_PYTHON_ENVIRONMENT_PROBE"
for i in range(25):
    os.environ[name] = str(i)
    assert os.getenv(name) == str(i)
del os.environ[name]
assert os.getenv(name) is None
print("PYTHON3_AMIX_STDLIB_PASS: zlib, gzip, zip, hashes, struct, datetime, math, select, environment")
