#!/usr/bin/env python3
"""mkbenchadf.py BOOT.bin PAYLOAD.bin OUT.adf -- c1bench.adf: boot block
(checksummed) and the payload from sector 2, the rest of an 880 KB disk zero."""
import struct
import sys

boot = bytearray(open(sys.argv[1], "rb").read())
payload = open(sys.argv[2], "rb").read()
if len(boot) > 1024:
    sys.exit("boot block over 1024 bytes")
boot += bytes(1024 - len(boot))
boot[4:8] = b"\0\0\0\0"
s = 0
for i in range(0, 1024, 4):
    s += struct.unpack(">I", boot[i:i + 4])[0]
    if s > 0xFFFFFFFF:
        s = (s & 0xFFFFFFFF) + 1
boot[4:8] = struct.pack(">I", (~s) & 0xFFFFFFFF)
adf = bytearray(901120)
adf[0:1024] = boot
adf[1024:1024 + len(payload)] = payload
open(sys.argv[3], "wb").write(adf)
print("%s: payload %d bytes" % (sys.argv[3], len(payload)))
