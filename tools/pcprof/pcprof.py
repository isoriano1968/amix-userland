#!/usr/bin/env python3
"""pcprof.py BINARY pcprof.out [N] -- per-function share of the samples (pcprof.c)"""
import subprocess, sys, bisect
binary, out = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 30
nm = subprocess.run([sys.argv[4] if len(sys.argv) > 4 else
                     __import__("os").path.expanduser("~/opt/m68k-amix/bin/m68k-elf-nm"), "-n", binary],
                    capture_output=True, text=True).stdout
syms = []
for line in nm.splitlines():
    p = line.split()
    if len(p) == 3 and p[1] in "tTwW":
        syms.append((int(p[0], 16), p[2]))
addrs = [a for a, _ in syms]
lines = open(out).read().split()
assert lines[0] == "pcprof"
low, shift, n, outside = int(lines[1], 16), int(lines[2]), int(lines[3]), int(lines[4])
counts = list(map(int, lines[5:5 + n]))
per = {}
total = 0
for i, c in enumerate(counts):
    if not c:
        continue
    pc = low + (i << shift)
    k = bisect.bisect_right(addrs, pc) - 1
    name = syms[k][1] if k >= 0 else "?"
    per[name] = per.get(name, 0) + c
    total += c
# outside: per 256-byte block, mapped into libc.so.1 if the libc line is there
rest = lines[5 + n:]
libc_base = None
outs = []
i = 0
while i < len(rest):
    if rest[i] == "libc":
        sym, addr = rest[i + 1], int(rest[i + 2], 16)
        libc = __import__("os").path.expanduser("~/opt/m68k-amix-sysroot/usr/lib/libc.so.1")
        dyn = subprocess.run([__import__("os").path.expanduser("~/opt/m68k-amix/bin/m68k-elf-nm"), "-n", libc],
                             capture_output=True, text=True).stdout
        lsyms = []
        for l in dyn.splitlines():
            q = l.split()
            if len(q) == 3 and q[1] in "tTwW":
                lsyms.append((int(q[0], 16), q[2]))
                if q[2] == sym:
                    libc_base = addr - int(q[0], 16)
        lsyms.sort()
        i += 3
    elif rest[i] == "o":
        outs.append((int(rest[i + 1], 16), int(rest[i + 2])))
        i += 3
    else:
        i += 1
if outs:
    byname = {}
    for blk, c in outs:
        name = "%08x" % blk
        if libc_base is not None and lsyms:
            off = blk - libc_base
            k = bisect.bisect_right([a for a, _ in lsyms], off + 0x80) - 1
            if 0 <= off < 0x100000 and k >= 0:
                name = "libc:" + lsyms[k][1]
        byname[name] = byname.get(name, 0) + c
    print("outside, by place (libc base %s):" % (hex(libc_base) if libc_base is not None else "?"))
    for name, c in sorted(byname.items(), key=lambda x: -x[1])[:15]:
        print("   %5d  %s" % (c, name))
print("%d samples: %d in the program, %d outside (shared libraries, system calls)"
      % (total + outside, total, outside))
total += outside
per["(outside: shared libs, syscalls)"] = outside
for name, c in sorted(per.items(), key=lambda x: -x[1])[:top]:
    print("%6.1f%%  %5d  %s" % (100.0 * c / total, c, name))
