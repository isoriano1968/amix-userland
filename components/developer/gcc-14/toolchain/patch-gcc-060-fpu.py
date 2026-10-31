#!/usr/bin/env python3
"""patch-gcc-060-fpu.py -- no FSIN/FCOS when tuning for the 68040/68060.

WHY
---
With -ffast-math (flag_unsafe_math_optimizations) GCC's m68k backend expands
sin() and cos() inline as the 68881 FSIN/FCOS instructions.  The 68040 and
68060 do not have them: every one traps into the kernel's FPU emulation.
Measured on the A3000's 68060 (AMIX): 78.6 us per inlined fsin, against
~3 us for a software sin.  m68k.md already keeps the other instructions the
060 emulates (64-bit mul/div, fscale, fmovecr) away from TUNE_68060 code;
the sin/cos patterns were only ever guarded by TARGET_68881.

This adds "&& !TUNE_68040_60" to both, so -m68040/-m68060 code calls sin()
and cos() like any other function (the compiler's default here is 68060).

Idempotent.  Usage: patch-gcc-060-fpu.py <gcc-src>/gcc/config/m68k/m68k.md
"""
import sys

path = sys.argv[1]
src = open(path).read()

done = 0
for name, unspec in (("sin", "UNSPEC_SIN"), ("cos", "UNSPEC_COS")):
    head = '(define_insn "%s<mode>2"' % name
    i = src.find(head)
    if i < 0:
        sys.exit("patch-gcc-060-fpu.py: %s pattern not found" % name)
    old = '"TARGET_68881 && flag_unsafe_math_optimizations"'
    new = '"TARGET_68881 && flag_unsafe_math_optimizations && !TUNE_68040_60"'
    j = src.find(old, i, i + 400)
    if j < 0:
        if src.find(new, i, i + 400) >= 0:
            continue
        sys.exit("patch-gcc-060-fpu.py: %s condition not found" % name)
    src = src[:j] + new + src[j + len(old):]
    done += 1

if done == 0:
    print("patch-gcc-060-fpu.py: already applied")
else:
    open(path, "w").write(src)
    print("patch-gcc-060-fpu.py: %d patterns guarded" % done)
