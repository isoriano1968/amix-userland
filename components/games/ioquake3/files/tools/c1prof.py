#!/usr/bin/env python3
"""c1prof.py ELF PROFILE [FROM-LINE [TO-LINE]] -- core 1's pc samples
(c1q3 -profile, 200 a second) by function, from the image's symbols
(arm-none-eabi-nm), most first; then by source group (engine, Mesa ...).

  files/tools/c1prof.py build/core1/q3.elf prof.txt 12000      # the last part only
"""
import bisect, collections, subprocess, sys

NM = __import__('os').path.expanduser('~/opt/arm-none-eabi/bin/arm-none-eabi-nm')


def main():
    elf, path = sys.argv[1], sys.argv[2]
    lo = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    hi = int(sys.argv[4]) if len(sys.argv) > 4 else None
    syms = []
    for line in subprocess.run([NM, '-n', '-S', '--defined-only', elf], capture_output=True, text=True).stdout.splitlines():
        f = line.split()
        if len(f) == 4 and f[2] in 'tTwW':
            syms.append((int(f[0], 16), int(f[1], 16), f[3]))
    addrs = [s[0] for s in syms]
    pcs = [int(l, 16) for l in open(path).read().split()][lo:hi]
    by = collections.Counter()
    for pc in pcs:
        i = bisect.bisect_right(addrs, pc) - 1
        by[syms[i][2] if i >= 0 and pc < syms[i][0] + max(syms[i][1], 4) else '?%08x' % (pc & ~0xfff)] += 1
    n = len(pcs) or 1
    print('%d samples (%.1f s)' % (len(pcs), len(pcs) / 200.0))
    for name, k in by.most_common(40):
        print('%6.2f%%  %6d  %s' % (100.0 * k / n, k, name))


if __name__ == '__main__':
    main()
