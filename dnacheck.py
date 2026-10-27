#!/usr/bin/env python3
"""dnacheck.py -- compare every struct size in obj/makesdna/DNA.c (what
makesdna computed on the A3000) with what gcc14 compiles for the target."""
import os, re, struct, subprocess
H = os.path.dirname(os.path.abspath(__file__))
TC = os.path.expanduser('~/opt/m68k-amix/bin/')
dna = open(H + '/obj/makesdna/DNA.c').read()
b = bytes(int(x) for x in re.findall(r'\d+', dna.split('};')[0].split('{', 1)[1]))
p = 4
def take(tag):
    global p
    p += 4; n, = struct.unpack('>I', b[p:p + 4]); p += 4; return n
def strs(n):
    global p
    o = []
    for _ in range(n):
        e = b.index(b'\0', p); o.append(b[p:e].decode()); p = e + 1
    return o
def al():
    global p
    p = (p + 3) & ~3
names = strs(take(b'NAME')); al()
types = strs(take(b'TYPE')); al()
p += 4; tlen = struct.unpack('>%dH' % len(types), b[p:p + 2 * len(types)]); p += 2 * len(types); al()
ns = take(b'STRC'); structs = []
for _ in range(ns):
    t, nm = struct.unpack('>HH', b[p:p + 4]); p += 4 + 4 * nm; structs.append(types[t])
d = H + '/src/source/blender/makesdna'
src = H + '/obj/link/dnacheck.c'
open(src, 'w').write('\n'.join('#include "%s"' % f for f in sorted(os.listdir(d)) if f.startswith('DNA_') and f.endswith('.h')) + '\n' +
                     '\n'.join('const int sz_%s = sizeof(struct %s);' % (s, s) for s in structs) + '\n')
subprocess.run([TC + 'm68k-elf-gcc', '-I', H + '/amixinc', '-O2', '-m68060', '-funsigned-char', '-D__amix__', '-D__BIG_ENDIAN__',
                '-std=gnu89', '-include', H + '/ccompat.h', '-w', '-I', d, '-I', H + '/src/source/blender/blenlib',
                '-I', H + '/src/intern/guardedalloc', '-c', src, '-o', src[:-2] + '.o'], check=True)
od = subprocess.run([TC + 'm68k-elf-objdump', '-t', '-s', '-j', '.rodata', src[:-2] + '.o'], capture_output=True, text=True).stdout
sym = {m.group(2): int(m.group(1), 16) for m in re.finditer(r'(?m)^([0-9a-f]+) g\s+O \.rodata\s+[0-9a-f]+ sz_(\w+)$', od)}
data = bytearray()
for m in re.finditer(r'(?m)^ ([0-9a-f]{4,}) ((?:[0-9a-f]{2,8} ?){1,4})', od):
    data += bytes.fromhex(m.group(2).replace(' ', ''))
bad = 0
for s in structs:
    real, = struct.unpack('>i', data[sym[s]:sym[s] + 4])
    if real != tlen[types.index(s)]:
        bad += 1; print('MISMATCH %-22s gcc14 %5d  DNA %5d' % (s, real, tlen[types.index(s)]))
print('structs %d, mismatches %d' % (len(structs), bad))
