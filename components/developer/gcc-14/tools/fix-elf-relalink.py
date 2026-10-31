#!/usr/bin/env python3
"""fix-elf-relalink.py -- repair sh_link on SHT_RELA sections in AMIX's 1991
static libraries.

WHY
---
Every relocatable object in /usr/X/lib/libX11.a carries its .rela.text with
sh_link pointing at itself instead of at .symtab.  sh_link on a SHT_RELA
section must name the symbol table its r_info indices refer to; pointing it
elsewhere makes the section uninterpretable as relocation metadata.

GNU ld's response is the damaging part, because it is silent.  It does not
apply those relocations -- so every pointer the object expected the linker to
fill stays zero -- and the default linker script's `*(.rela.text)` pattern then
collects the section into the output's .rela.dyn by name, where its
section-relative offsets look like dynamic relocations aimed outside the image.

The observed result: a binary that merely references XOpenDisplay segfaults
before main, and OpenTTD dies at its first Xlib call with pc=0x00000000.

This rewrites sh_link in place.  It changes four bytes per RELA section and
touches nothing else -- not the relocations, not the symbols, not the code.

Usage:
    fix-elf-relalink.py [--dry-run] FILE...        # relocatable .o files
"""
import struct, sys, os

SHT_SYMTAB, SHT_RELA = 2, 4

def fix(path, dry):
    with open(path, "rb") as f:
        d = bytearray(f.read())
    if d[:4] != b"\x7fELF":
        return None                      # not ELF: archive members can be odd
    if d[4] != 1 or d[5] != 2:
        raise SystemExit("%s: only 32-bit big-endian is handled" % path)

    e_shoff, = struct.unpack_from(">I", d, 0x20)
    e_shentsize, e_shnum = struct.unpack_from(">HH", d, 0x2e)
    if e_shoff == 0 or e_shnum == 0:
        return None

    def sh(i, field):                    # field offsets inside Elf32_Shdr
        return struct.unpack_from(">I", d, e_shoff + i * e_shentsize + field)[0]

    symtab = None
    for i in range(e_shnum):
        if sh(i, 4) == SHT_SYMTAB:
            symtab = i
            break
    if symtab is None:
        return None                      # nothing to point at; leave it alone

    fixed = []
    for i in range(e_shnum):
        if sh(i, 4) != SHT_RELA:
            continue
        link = sh(i, 24)
        if link == symtab:
            continue
        fixed.append((i, link, symtab))
        if not dry:
            struct.pack_into(">I", d, e_shoff + i * e_shentsize + 24, symtab)

    if fixed and not dry:
        with open(path, "wb") as f:
            f.write(d)
    return fixed

def main():
    args = sys.argv[1:]
    dry = False
    if args and args[0] == "--dry-run":
        dry, args = True, args[1:]
    if not args:
        raise SystemExit(__doc__)

    files = touched = sections = 0
    for p in args:
        r = fix(p, dry)
        if r is None:
            continue
        files += 1
        if r:
            touched += 1
            sections += len(r)
            for i, was, now in r:
                print("%s: section %d sh_link %d -> %d" % (os.path.basename(p), i, was, now))
    print("%s%d file(s) examined, %d changed, %d RELA section(s)"
          % ("DRY RUN: " if dry else "", files, touched, sections))

main()
