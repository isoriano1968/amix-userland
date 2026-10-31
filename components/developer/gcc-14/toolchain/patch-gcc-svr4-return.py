#!/usr/bin/env python3
"""patch-gcc-svr4-return.py -- make m68k-elf return pointers the SVR4 way.

WHY
---
m68k SVR4, and so AMIX, returns a pointer in %a0 AND %d0: the callee leaves it
in %a0 and copies it to %d0 in the epilogue.  AMIX's libc does that, and
gcc 2.7.2.3 -- the compiler every vendor and R6.3 library on the machine was
built with -- reads a pointer result from %a0.  GCC 14's m68k-elf (through
m68k/m68kemb.h) returns everything in %d0 only.

The two halves of the problem are not symmetric, which is why it went unseen:

  * gcc14 code calling AMIX libc: libc sets both registers, gcc14 reads %d0,
    fine.  Every probe in this repository is this case.
  * gcc-2 code calling a gcc14 function that returns a pointer: gcc-2 reads
    %a0, which holds whatever the callee last left there.  Measured with
    CDE on the R6.3 libraries: libXt (53 gcc-2 objects) calling gcc14-built
    Motif/CDE converters and allocators got garbage and crashed far away,
    looking like heap corruption.

GCC already has the right code: m68k_function_value() in m68k.cc, used by
m68k/linux.h and m68k/netbsd-elf.h, returns a PARALLEL of %a0 and %d0 for a
function declared to return a pointer.  This replaces m68kemb.h's D0-only
definitions with linux.h's.  m68kemb.h is the last ABI header m68k-elf reads,
so changing it is enough, and no config.gcc change is needed.

Idempotent.  Usage: patch-gcc-svr4-return.py <gcc-src>/gcc/config/m68k/m68kemb.h
"""
import sys

path = sys.argv[1]
src = open(path).read()

MARK = "/* AMIX: m68k SVR4 return registers"
if MARK in src:
    print("patch-gcc-svr4-return.py: already applied")
    sys.exit(0)

OLD = """#undef FUNCTION_VALUE
#define FUNCTION_VALUE(VALTYPE,FUNC) LIBCALL_VALUE (TYPE_MODE (VALTYPE))

#undef LIBCALL_VALUE
#define LIBCALL_VALUE(MODE)					\\
  m68k_libcall_value (MODE)

#undef FUNCTION_VALUE_REGNO_P
#define FUNCTION_VALUE_REGNO_P(N)			\\
  ((N) == D0_REG || (TARGET_68881 && (N) == FP0_REG))
"""

NEW = """/* AMIX: m68k SVR4 return registers, as m68k/linux.h: integers in d0,
   pointers in a0 copied to d0, floats in fp0.  gcc 2.7 code on AMIX reads a
   pointer result from a0.  See toolchain/patch-gcc-svr4-return.py.  */
#undef FUNCTION_VALUE
#define FUNCTION_VALUE(VALTYPE,FUNC) m68k_function_value (VALTYPE, FUNC)

#undef LIBCALL_VALUE
#define LIBCALL_VALUE(MODE)					\\
  m68k_libcall_value (MODE)

#undef FUNCTION_VALUE_REGNO_P
#define FUNCTION_VALUE_REGNO_P(N)			\\
  ((N) == D0_REG || (N) == A0_REG || (TARGET_68881 && (N) == FP0_REG))
"""

if OLD not in src:
    sys.exit("patch-gcc-svr4-return.py: %s does not have the expected "
             "FUNCTION_VALUE block -- a different GCC version?" % path)
open(path, "w").write(src.replace(OLD, NEW))
print("patch-gcc-svr4-return.py: patched", path)
