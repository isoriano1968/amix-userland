#!/usr/bin/env python3
"""Is this binary free of the instructions the 68060 removed?

Answered from the ENCODING, not from the disassembler's mnemonics, because
those are actively misleading here.  objdump prints `divul` for the 64-bit
form and `divull` for the 32-bit one -- the extra L means "long form with a
remainder register", not "longer dividend" -- so a grep for the name that looks
dangerous finds the safe one and vice versa.  Counting mnemonics has now sent
this project down the wrong path twice.

The 68060 removed:
    MULU.L/MULS.L  <ea>,Dh:Dl    32x32 -> 64
    DIVU.L/DIVS.L  <ea>,Dr:Dq    64/32
It kept the 32-bit forms of both, which share the same opcode and differ only
in bit 10 of the extension word:

    MUL: 0100 1100 00 <ea>   ext: 0 Dl 1 SIZE 0000000 Dh   (bit 10 = SIZE)
    DIV: 0100 1100 01 <ea>   ext: 0 Dq 1 SIZE 0000000 Dr

SIZE = 1 means the 64-bit form, which traps on a 68060 without an Integer
Support Package and, on a kernel that declines it, kills the process outright.

TWO WAYS TO GET A WRONG ANSWER, both closed here rather than documented:

  Handed a BINARY instead of a disassembly listing, a line-matching tool finds
  nothing and reports clean.  That is a plausible answer where a loud one was
  available -- the exact failure this tool exists to avoid -- so an ELF file is
  disassembled here instead of being silently misread.  (scan060.py in a
  sibling AMIX project has the same shape; the warning came from there.)

  objdump disassembles DATA as if it were code, so a constant pool can produce
  instructions that were never executed.  Only sections objdump marks as
  disassembled and that are executable are considered.

  That filter is NOT sufficient and it is worth being precise about why: gcc
  puts switch dispatch tables in .text, immediately after the jmp that reads
  them, so misdecoded data survives a section filter completely.  A sibling
  project found fourteen "MOVEP" instructions in a Doom binary this way, every
  one of them a monotonically increasing 16-bit offset in such a table.

  What saves this tool from that is the direction of the error rather than any
  cleverness -- but only within a limit that is easy to lose.  Data decoding as
  an instruction can only ADD hits while the misdecoded bytes ARE data: data
  holds no real instruction to lose.  It does not hold in general.  If the
  decoder loses sync inside real code, a real instruction can be swallowed as
  another one's operand and never appear in the listing -- and that is the
  direction in which a clean verdict is WRONG rather than merely conservative.

  A CLEAN VERDICT IS A PROPERTY OF THE SOURCE AS IT STANDS, NOT OF THE PROGRAM.
  The 68060 removed the 64-bit divide, and C's `%` on 64-bit operands is what
  emits it.  A binary that scans clean today becomes fatal the moment someone
  writes one modulo -- and on a kernel whose vector-61 handler declines the
  instruction, a decline is SIGKILL rather than slowness.  So this is a gate to
  run on every build, not a certificate to file once.  (Learned from a sibling
  port that had filed its own clean scan as a durable result.)

  So the tool checks for lost sync instead of assuming it away.  objdump prints
  a label at each symbol, and if the decoder is aligned the next instruction
  begins at exactly that address; if it does not, the bytes before it were
  decoded off-boundary.  A listing with such gaps is reported as untrustworthy
  rather than clean, which is the only honest verdict available.

  Candidates are printed with their surroundings for the other half: a run of
  increasing 16-bit values after a jmp is a table, not code.

Usage: check-68060-safe.py <binary or objdump -d listing> [...]
Exit status 0 if clean, 1 if any 64-bit form is present, 2 if the input could
not be read as either.
"""
import os
import re
import subprocess
import sys

# objdump line: "  80029bd0:\t4c7c 0800 0000 \tdivsll #1000,%d0,%d0"
LINE = re.compile(r"^\s*([0-9a-f]+):\s*((?:[0-9a-f]{4} )+)\s*(\S+)\s*(.*)$")

def die(msg):
    sys.stderr.write("check-68060-safe.py: %s\n" % msg)
    sys.exit(2)


def objdump_candidates():
    """Where to find an objdump that reads m68k.  config.sh knows, when this is
    run from the repository; the PATH names are the fallback."""
    names = []
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cfg = os.path.join(here, "config.sh")
    if os.path.exists(cfg):
        prefix = target = None
        for line in open(cfg):
            line = line.strip()
            if line.startswith("TC_PREFIX="):
                prefix = os.path.expandvars(line.split("=", 1)[1].strip().strip('"'))
            elif line.startswith("TC_TARGET="):
                target = line.split("=", 1)[1].strip().strip('"')
        if prefix and target:
            names.append(os.path.join(prefix, "bin", "%s-objdump" % target))
    names += ["m68k-elf-objdump", "m68k-linux-gnu-objdump", "objdump"]
    return names


def listing_for(path):
    """A disassembly listing, whether given one or an ELF file."""
    with open(path, "rb") as f:
        magic = f.read(4)
    if magic != b"\x7fELF":
        text = open(path, errors="replace").read()
        if "Disassembly of section" not in text:
            die("%s is neither an ELF file nor an objdump disassembly -- "
                "refusing to report it clean" % path)
        return text
    for objdump in objdump_candidates():
        try:
            out = subprocess.check_output([objdump, "-d", path],
                                          stderr=subprocess.DEVNULL).decode("latin1")
        except (OSError, subprocess.CalledProcessError):
            continue
        if "Disassembly of section" in out:
            return out
    die("%s is an ELF file but no objdump here could disassemble it (tried %s)"
        % (path, ", ".join(objdump_candidates())))


# Sections that hold instructions.  Anything else objdump disassembles is data
# that happens to decode.
CODE_SECTIONS = (".text", ".init", ".fini", ".plt")
SECTION = re.compile(r"^Disassembly of section (\S+):")
LABEL = re.compile(r"^([0-9a-f]+) <([^>]+)>:")

bad = []
total_long = 0
desync = []          # labels the decoder did not land on
pending_label = None

for path in sys.argv[1:]:
    func = "?"
    in_code = False
    lines = listing_for(path).split("\n")
    for idx, line in enumerate(lines):
        m_lbl = LABEL.match(line)
        if m_lbl:
            func = line.strip()
            # Remember where the next instruction is expected to begin.
            pending_label = (int(m_lbl.group(1), 16), m_lbl.group(2)) if in_code else None
            continue

        m_sec = SECTION.match(line)
        if m_sec:
            name = m_sec.group(1).rstrip(":")
            in_code = name.startswith(CODE_SECTIONS)
            continue
        if not in_code:
            continue
        m = LINE.match(line)
        if not m:
            continue

        if pending_label is not None:
            want, name = pending_label
            pending_label = None
            if int(m.group(1), 16) != want:
                desync.append((want, name, int(m.group(1), 16)))
        words = m.group(2).split()
        if len(words) < 2:
            continue
        op = int(words[0], 16)
        # 0x4C00-0x4C7F is the long multiply/divide group; bit 6 picks divide.
        if (op & 0xFF80) != 0x4C00:
            continue
        total_long += 1
        ext = int(words[1], 16)
        if ext & 0x0400:            # SIZE bit: 64-bit form
            ctx = [l for l in lines[max(0, idx - 3):idx + 4] if l.strip()]
            bad.append((m.group(1), m.group(3), m.group(4), func, ctx))

if desync:
    print("UNTRUSTWORTHY: the decoder lost sync at %d symbol(s), so a real"
          % len(desync))
    print("instruction may have been swallowed and never listed.  A clean")
    print("verdict cannot be given for this listing.")
    for want, name, got in desync[:8]:
        print("  %s expected at 0x%08x, next instruction at 0x%08x" % (name, want, got))
    if len(desync) > 8:
        print("  ... and %d more" % (len(desync) - 8))
    sys.exit(3)

if total_long == 0:
    print("no long multiply or divide instructions at all -- nothing to check")

if bad:
    print("NOT 68060-safe: %d candidate(s) using a form the 68060 removed" % len(bad))
    print("Each is printed with its surroundings: objdump decodes data as code,")
    print("and gcc puts switch tables in .text, so check for a jmp followed by a")
    print("run of increasing 16-bit values before believing any single hit.")
    for addr, mnem, ops, func, ctx in bad[:12]:
        print("\n  %s in %s" % (mnem, func))
        for line in ctx:
            marker = ">>" if line.lstrip().startswith(addr) else "  "
            print("   %s %s" % (marker, line.rstrip()))
    if len(bad) > 12:
        print("\n  ... and %d more" % (len(bad) - 12))
    sys.exit(1)

print("68060-safe: %d long multiply/divide instructions, none in a 64-bit form"
      % total_long)
