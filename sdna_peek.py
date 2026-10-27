#!/usr/bin/env python3
"""sdna_peek.py -- read fields out of a .blend (or Blender's B.blend.c) with
the file's own SDNA, independent of any build: FILE STRUCT FIELD.FIELD..."""
import re, struct, sys

def load(path):
    d = open(path, 'rb').read()
    if path.endswith('.c'):
        d = bytes(int(x) for x in re.findall(r'\b\d+\b', d.decode().split('{', 1)[1].split('}')[0]))
    return d

def main():
    path, sname, fpath = sys.argv[1], sys.argv[2], sys.argv[3]
    d = load(path)
    ptr = 4 if d[7:8] == b'_' else 8
    E = '<' if d[8:9] == b'v' else '>'
    hl = 16 + ptr
    off, blocks = 12, []
    while off + hl <= len(d):
        code = d[off:off + 4]
        ln, = struct.unpack(E + 'I', d[off + 4:off + 8])
        sdna, nr = struct.unpack(E + 'II', d[off + 8 + ptr:off + 16 + ptr])
        blocks.append((code, off + hl, ln, sdna, nr))
        if code == b'ENDB':
            break
        off += hl + ln
    dna = [b for b in blocks if b[0] == b'DNA1'][0]
    b = d[dna[1]:dna[1] + dna[2]]
    p = 4
    def take():
        nonlocal p
        p += 4; n, = struct.unpack(E + 'I', b[p:p + 4]); p += 4; return n
    def strs(n):
        nonlocal p
        o = []
        for _ in range(n):
            e = b.index(b'\0', p); o.append(b[p:e].decode()); p = e + 1
        return o
    def al():
        nonlocal p
        p = (p + 3) & ~3
    names = strs(take()); al()
    types = strs(take()); al()
    p += 4; tlen = struct.unpack(E + '%dH' % len(types), b[p:p + 2 * len(types)]); p += 2 * len(types); al()
    ns = take(); structs = []
    for _ in range(ns):
        t, nm = struct.unpack(E + 'HH', b[p:p + 4]); p += 4
        f = [struct.unpack(E + 'HH', b[p + 4 * i:p + 4 * i + 4]) for i in range(nm)]; p += 4 * nm
        structs.append((types[t], f))
    sidx = {s[0]: i for i, s in enumerate(structs)}
    def fsize(t, n):
        if n.startswith('*') or n.startswith('(*'):
            base = ptr
        else:
            base = tlen[types.index(t)]
        m = 1
        for a in re.findall(r'\[(\d+)\]', n):
            m *= int(a)
        return base * m
    def offset(sn, parts):
        o = 0
        for part in parts:
            for t, n in structs[sidx[sn]][1]:
                tn, nn = types[t], names[n]
                bare = re.sub(r'[\*\(\)]|\[.*', '', nn)
                if bare == part:
                    sn = tn; break
                o += fsize(tn, nn)
            else:
                sys.exit('no field %s' % part)
        return o, sn, nn
    # the struct's first block
    for code, at, ln, sdna, nr in blocks:
        if structs[sdna][0] == sname:
            o, t, n = offset(sname, fpath.split('.'))
            sz = tlen[types.index(t)]
            raw = d[at + o:at + o + sz]
            fmt = {1: 'B', 2: 'h', 4: 'i'}.get(sz)
            v = struct.unpack(E + fmt, raw)[0] if fmt else raw
            print('%s %s.%s (%s %s, offset %d) = %r  [file %s-endian, ptr %d]' % (code, sname, fpath, t, n, o, v, 'little' if E == '<' else 'big', ptr))
            return
    sys.exit('no %s block' % sname)

main()
