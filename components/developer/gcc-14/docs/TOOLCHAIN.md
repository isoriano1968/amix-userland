# The toolchain question

OpenTTD is C++. AMIX's compiler is GCC 2.7.2.3, which predates the language
OpenTTD is written in and has no C++ front end installed at all. So before any
porting work is worth planning, one question has to be answered: **can a modern
compiler produce binaries that AMIX will run?**

It can. This document records the measurements, because every one of them is a
thing that could have been assumed wrongly, and each was cheap to check and
expensive to guess.

Target machine for all runtime results: Amiga 3000, Mercury 68060, AMIX SVR4
2.1c with the ported 68040/68060 kernel, 32 MB.

A note on identifying that kernel, because it caught us: **`uname` does not name
the image.** The port ships one image that boots both CPUs, with `cputype` poked
at boot from `AttnFlags`, and the prefix `uname -m` prints is a readout of the
CPU it is *running on* — not the build. So the same file reports
`68040-260903-04` in its own strings and `68060-260903-04` through `uname` on a
68060. Everything measured here was measured on image `68040-260903-04` running
on a 68060, and "the 68060 kernel" is not a thing that exists.

To identify an image for real, read a counter block's magic word at an address
computed from the candidate file — a method that fails loudly rather than
plausibly.

## 1. AMIX's ELF is the plain SVR4 ELF

Read off the machine's own `/usr/bin/ls`:

| Property | Value |
|---|---|
| Class / data | ELF32, big endian, `UNIX - System V`, machine `MC68000` |
| Interpreter | `/usr/lib/libc.so.1` — libc *is* the runtime linker |
| Text segment | `0x80000000` |
| Sections | `.interp .hash .dynsym .dynstr .rela.plt .init .plt .text .fini .data .got .dynamic .bss` |
| Dynamic tags | `NEEDED INIT FINI HASH STRTAB SYMTAB STRSZ SYMENT DEBUG PLTGOT PLTRELSZ PLTREL JMPREL` |
| `SYMENT` | 16 — the standard `Elf32_Sym` |
| Relocations | `R_68K_JMP_SLOT` |
| Segment alignment | `0x2000` |

Nothing here is unusual. There is no vendor extension to reproduce, no odd
symbol table layout, no pre-ELF relic. A modern linker can be told to emit
exactly this.

## 2. A modern compiler accepts the 1991 headers

GCC 14 compiles against `$AMIX_SYSROOT/usr/include` with `-nostdinc`, without a
single warning. The SVR4.0 headers are plain C89 with `__STDC__` guards, which is
the dialect modern GCC still reads.

## 3. A modern linker emits an AMIX-shaped executable

Linked with binutils 2.4x against AMIX's own `crt1.o`/`crti.o`/`crtn.o` and
`libc.so.1`, the result is structurally the same file. The PLT is the giveaway —
ours and the machine's are the same instruction sequence:

```
    jmp   %pc@(<got slot>)@(0)      ; 4efb 0171 ....
    movel #<reloc index>,%sp@-      ; 2f3c
    bral  <plt0>                    ; 60ff
```

The only differences were two extra program headers GNU tools add by habit:
`PT_GNU_RELRO` (removed with `-z norelro`) and `PT_GNU_STACK` (which modern `ld`
refuses to drop for this target — `-z nognustack` is ignored). **AMIX's loader
ignores the unknown segment**, verified by running the binary; see §5.

## 4. The m68k ABI matches, with one exception

Verified by disassembling AMIX libc and comparing against GCC 14's output for
the same construct:

| Property | AMIX (AT&T cc, 1991) | GCC 14 m68k | Verdict |
|---|---|---|---|
| Pointer return | `%a0` and `%d0` (`strcpy` ends `movel %a0,%d0`) | same | match |
| `char` | signed | signed | match |
| Struct alignment | 2 bytes (`{char;int;short}` → size 8, `int` at offset 2) | same | match |
| `long double` | 96-bit extended, 12 bytes | 12 bytes | match |
| Struct return pointer | **`%a0`** (`div` stores through `%a0` with no load from the stack) | **`%a1`** | **differs** |

The struct-return register is the one real mismatch, and it is narrow: it only
matters when *calling a precompiled AMIX function that returns a struct by
value*. In libc that is `div()` and `ldiv()`; Xlib returns no struct by value.
Two ways out, in order of preference:

1. Provide `div`/`ldiv` in the port's own compat layer, so the boundary is never
   crossed. Cheap, local, no compiler change.
2. Override `M68K_STRUCT_VALUE_REGNUM` (defined as `A1_REG` in
   `gcc/config/m68k/m68k.h`) in a target header. Correct, but it means carrying
   a GCC patch.

Note that `-fpcc-struct-return` does **not** fix this: it moves the return into
memory, but still passes the address in `%a1`.

## 5. It runs on the real machine

```
$ tools/amix-cc.sh -o probe/hello -O2 probe/hello.c
$ tools/push-run.sh probe/hello
hello from a modern m68k gcc
  argc = 1, argv[0] = ./hello
exit status: 0
```

and the ABI battery (`probe/interop.c`), on the 68060:

```
sizes:   short=2 int=4 long=4 ptr=4 longlong=8
packing: sizeof=8 offsetof(i)=2   (SVR4 m68k wants 8 and 2)
char:    signed
mul64:   8641969
div64:   1234567890
heap:    libc string functions (len 21)
time:    tv_sec=832931896
fp:      1/3 = 0.333333
done
```

Three things worth naming in that output. The 64-bit results prove GCC's own
`libgcc` helper routines interoperate with our code. `heap`/`time` prove
`malloc`, the string functions and a syscall wrapper all cross the boundary
intact. And **`fp` proves the 68060 FPU path works** — including libc's
floating-point `printf`, which was the least certain item on the list, since the
68060 lacks instructions the 68881 has and the kernel's `ifpsp060` integration
is listed as incomplete.

## 6. What the packaged compiler cannot do

Debian ships `g++-14-m68k-linux-gnu`, and it is tempting. It cannot give us
working C++ exceptions:

```
$ m68k-linux-gnu-nm -u /usr/lib/gcc-cross/m68k-linux-gnu/14/libgcc_eh.a
    U _dl_find_object
    U pthread_cond_broadcast
    U pthread_key_create
    U pthread_mutex_lock
    ... (and the rest of the pthread family)
```

That is the glibc-flavoured unwinder: it finds exception tables by asking the
dynamic loader, and it locks with pthreads. AMIX has neither. An `m68k-elf`
target instead builds the *registry* unwinder, where `crtbegin` registers
`.eh_frame` from the `.init` section at startup and no loader cooperation is
needed; `--disable-threads` reduces the locking to no-ops. That is the reason
`toolchain/build-cross.sh` builds a compiler instead of installing one.

## 7. What the old compiler cannot do

Measured, not assumed:

- `m68k-cbm-sysv4-gcc -m68060` → ``cc1: Invalid option `68060' ``. Every line of
  C on this 68060 machine is currently compiled for a 68040 at best. The
  *assembler* (gas 2.8.1) does know the 68060.
- C99 is rejected: `for (int i = 0; ...)` → `parse error before 'int'`.
- No `cc1plus` is installed, and GCC 2.7.2.3's C++ predates the 1998 standard
  regardless.
- Its known miscompilations are already worked around by hand elsewhere in this
  project family: the Wolf3D AMIX build has to compile `wl_draw.c` with no `-O`
  because the optimiser hangs, and `wl_main.c` *with* `-O` because without it
  `InitGame` is generated wrongly.

For balance: on a tight hand-shaped loop the old compiler is not bad. Compiling
the 8bpp→RGB565 span an X11 video driver needs, GCC 2.7.2.3 produces five
instructions per pixel using a scaled index, GCC 14 produces six using
auto-increment. The case for modernising is correctness, language support and
68060 tuning — not per-instruction quality.

## 8. Library gaps to expect

AMIX is SVR4.0, which predates C99 and a good deal of what portable code
assumes. Confirmed absent from `libc.a`:

- `snprintf`, `vsnprintf` — OpenTTD uses these throughout
- `usleep`

Confirmed present: `gettimeofday`, `select`, `poll`, `mmap`, `shmget`, `strdup`,
`memmove`.

Also: the headers are incomplete against their own library. `<sys/time.h>`
declares `adjtime`, `getitimer` and `setitimer` but **not `gettimeofday`**, even
though libc exports it. A port has to declare what it uses.

## 9. The 68060 removes instructions libgcc uses

This one cost the most to find and matters beyond this port.

The compat layer's `snprintf` died on `%lld` and on nothing else. Two suspects
were cleared on the machine first: `va_arg(ap, long long)` reads the right eight
bytes even when the `va_list` was passed to another function, and 64-bit
division works. Then the second of those turned out to be an artefact of the
probe -- it divided by the *constant* 10, which GCC folds into a
multiply-by-reciprocal and never calls libgcc for at all.

Dividing by a variable is a different matter:

```
step 1: va_arg, no arithmetic
va_arg in callee:          hi=0000011f lo=71fb04cb  (want hi=0000011f lo=71fb04cb)
step 2: __udivdi3
Killed
```

The reason is in libgcc itself. Its `__udivmoddi4`, compiled for a 68020, uses

```
divul %d7,%d2,%d1        /* 32x64 divide */
divull %d4,%d3,%d3
mulul ...                /* 64-bit multiply */
```

and **the 68060 removed exactly those forms.** They are meant to be emulated by
Motorola's integer support package in the kernel; this machine's kernel has no
ISP handler wired in yet, so the unimplemented-instruction trap becomes a fatal
signal. `Killed` rather than a bus error, which is why it read as memory
exhaustion at first.

The fix is `--with-cpu=68060`, and it has to be the compiler's **default**:
passing `CFLAGS_FOR_TARGET=-mcpu=68060` to a libgcc rebuild does not reach
libgcc's own compile -- measured, the instructions survived it. Told the CPU is
a 68060, GCC avoids those forms everywhere, and the result still runs on
68020/030/040, because it only ever avoids instructions and never adds any.

Worth noting for the kernel side of this project family: a 68060 without the
ISP cannot run *any* 68020-targeted C that divides 64-bit integers by a
variable, which includes anything compiled by a stock m68k toolchain. This is a
much larger blast radius than one printf conversion.

## 10. Building the toolchain: what the generic target does not know

`m68k-elf` is a bare-metal target and assumes newlib. Four things had to be
supplied or worked around; all four are in `toolchain/build-cross.sh` with the
reasoning attached.

- **`--disable-gcov`.** libgcov wants `MAP_ANONYMOUS` and `intptr_t`; SVR4.0 has
  neither, and the libgcc build stops in `_gcov_merge_topn.o`.
- **`--disable-multilib`.** The target otherwise builds about fifteen multilibs,
  nearly all ColdFire, each with its own libgcc and libstdc++. On a host with a
  few gigabytes free that fills the disk before the build ends.
- **A specs file** (`toolchain/install-specs.sh`). The stock specs are bare:
  startfile is just `crtbegin.o`, lib is just `-lc`, link is empty. So the
  compiler cannot link at all -- AMIX keeps its link-time `libc.so` in
  `usr/ccs/lib`, which is on no default search path -- and even if it could, the
  result would have no entry point, no interpreter and the wrong text address.
  A specs file is the supported way to retarget a GCC without patching it, and
  everything in ours is guarded on `%{!nostdlib:...}` so `tools/amix-cc.sh`,
  which names every object itself, is unaffected.
- **A one-case patch to libstdc++'s generated configure**
  (`toolchain/patch-libstdcxx-configure.py`). `crossconfig.m4` ends in a case
  that rejects unrecognised targets, and `m68k-elf` is not on its list. Since
  this is a freestanding configuration, the honest entry is an empty one.

A compiler that cannot link is not merely inconvenient: libstdc++'s configure
runs link tests, and a failure makes it set `GCC_NO_EXECUTABLES` and then abort
with *"Link tests are not allowed after GCC_NO_EXECUTABLES"*. Both drivers need
the specs -- the installed one, and the build tree's `xgcc`, which reads specs
from its own directory.

## 11. The C library gaps, measured

`snprintf` is absent from the real machine, not just from an old image. The
native linker on AMIX 2.1c says so:

```
Undefined                       first referenced
 symbol                             in file
snprintf                            /var/tmp/cca000Se1.o
```

`compat/amix_compat.c` supplies it, and borrows libc's own formatter one
conversion at a time rather than reimplementing formatting -- with one
exception. **libc's formatter does not understand C99's `ll`:**

```
snprintf(b, n, "%lld", 1234567890123LL)   ->  "287"
```

That cannot be worked around at the call site. A `long long` occupies eight
bytes of the argument list whatever conversion you write, so printing it as
`%ld` does not merely truncate the value, it leaves the formatter four bytes out
of step and every later argument comes out as garbage. So the `ll` conversions
are rendered by hand and everything else is delegated. It matters for OpenTTD
specifically: money is `int64`, and `stdafx.h` routes 64-bit output through
`OTTD_PRINTF64` / `OTTD_PRINTFHEX64`, both of which expand to `ll` forms.

The headers are also incomplete against their own library. `<sys/time.h>`
declares `adjtime`, `getitimer` and `setitimer` but not `gettimeofday`;
`<sys/select.h>` defines `fd_set` and the `FD_*` macros but does not declare
`select`. Both symbols are exported by libc. A port has to declare what it uses.

The formatter's test battery runs **both** natively and on the Amiga
(`tools/test-compat-host.sh`, `probe/compat.c`). That is not redundancy: the
battery passed on the host and died on the target, and that difference is what
localised the 68060 fault. Keep both.

## 12. C++ runs on AMIX

The gate, on the real machine:

```
$ tools/amix-cc.sh -o probe/cxxprobe -O2 probe/cxxprobe.cpp
$ tools/push-run.sh probe/cxxprobe
1 templates:  maximum(3,7)=7 maximum(2.5,1.5)=2.5
2 virtual:    area=36 (want 36)
3 new/delete: area=81 (want 81)
3 new/delete: destructor returned
4 throw/catch: about to throw from four frames down
4 throw/catch: caught, why="unwound from four frames down"
all four ok
```

Step 4 was the one genuine unknown, and it is the interesting result:
**DWARF exception unwinding works with no cooperation from the AMIX runtime
linker at all.** The exception tables are registered by `crtbegin.o` from the
`.init` section at startup, which is what an `elf` target's libgcc does when it
cannot rely on `PT_GNU_EH_FRAME` and a loader that understands it. That is the
reason this toolchain is built rather than installed, and it is now proven
rather than argued.

Two more things this required, both found the hard way.

**AMIX's C headers have no C++ linkage guards.** They are 1991 SVR4 headers: no
`#ifdef __cplusplus / extern "C"` anywhere. So a C++ file that includes
`<stdio.h>` gets a C++-mangled `printf` and does not link:

```
undefined reference to `_Z6printfPKcz'
```

That is not a corner case, it is every C header for every C++ file. The fix is
the one C++ compilers of that era used: `toolchain/make-cxx-wrappers.sh`
generates a wrapper per header, earlier on the include path, each opening
`extern "C"` and reaching the original with `#include_next`. 495 of them, from
whatever the installation actually has, because a hand-written list would
quietly miss one. Nothing in the vendor tree is modified and C compilation never
sees them.

**There is no `libgcc_eh.a` in a `--disable-shared` build** — the unwinder is
inside `libgcc.a`. `tools/amix-cc.sh` asks rather than assuming, so it works
against a toolchain built either way.

## 13. A hosted libstdc++, and what OpenTTD needs from it

The first working toolchain was `--disable-hosted-libstdcxx`, which was the
right first step: it builds `libsupc++` — `operator new`, RTTI, `__cxa_throw` —
and that is what the C++ gate needs. It is **not** enough for OpenTTD.

Measured in the 1.0.5 sources, outside `3rdparty` and the host-side build
tools: `std::map` in 18 files, `std::list` in 11, `std::set` in 2,
`std::vector` in 1, `std::string` in 2 (the tar reader), and 28 uses of
`dynamic_cast`. There are **no iostreams anywhere**, which is the good news:
iostreams are the part of libstdc++ that drags in locales, facets and wide
characters, and they are not needed.

A freestanding install does not carry those headers -- `<map>`, `<list>`,
`<set>`, `<string>`, `<vector>` and `<stdexcept>` are all absent -- and the
freestanding `libstdc++.a` carries none of the out-of-line parts the containers
call: no `_Rb_tree_insert_and_rebalance`, no `__throw_out_of_range`, no
`basic_string` instantiation. Header-only is not the same as
library-independent.

The hosted build now works, and `probe/stl.cpp` passes on the machine:

```
1 vector:   size=5 back=16
2 list:     size=3 sum=6
3 map (red-black tree, out of line in libstdc++):  size=20 m[3]=9
4 set:      size=2 first=1
5 string, and a map keyed by one:  "data/opengfx.grf" len=16  lookup=42
6 dynamic_cast (RTTI):  hit=99 miss=null
7 out_of_range from libstdc++, caught
all seven done
```

Getting there needed §14 — the header repairs — plus one more gap in the C
library itself. The Itanium C++ ABI expects `__cxa_atexit` from libc: the
compiler emits a call to it for every static object with a destructor. AMIX
knows only `atexit(void (*)(void))`, which cannot carry the object pointer, so
libstdc++ does not link. `compat/amix_cxa.c` supplies it, growing its list on
demand rather than using a fixed table, because silently dropping a destructor
past some arbitrary count is a bug that shows up as a file not flushed on a
machine nobody can attach a debugger to. Configuring GCC with
`--disable-__cxa_atexit` was the alternative; filling the gap in the C library
is preferred, because that is where the gap is, and everything else built with
this compiler gets working static destructors too.

## 14. Repairing headers that predate the language

Going from `libsupc++` to a **hosted** libstdc++ meant compiling libstdc++'s own
sources against a 1991 C library, and that is where AMIX's age stops being a
curiosity and starts being work. Four repairs were needed, and every one of them
would have hit OpenTTD too.

**K&R function definitions inside the headers.** AMIX implements `stat`,
`lstat`, `fstat`, `mknod` and `uname` as `static` functions defined in the
header itself -- wrappers around the versioned `_xstat` family -- written in
1978 syntax:

```c
static int
stat(path, buf)
const char *path;
struct stat *buf;
{ ... }
```

C has always accepted that. C++ never has, so **every C++ file that includes
`<sys/stat.h>` fails**, with errors that point at the system header and read as
if your program were at fault (`'int stat' redeclared as different kind of
entity`). Five headers in a stock installation do this.
`toolchain/make-kandr-fixes.py` rewrites them into ANSI form from the local
sysroot, into GCC's `include-fixed` -- the directory that exists precisely for
system headers that are wrong or older than the language, and which is searched
before the sysroot. The vendor tree is untouched, and the generated output
contains AMIX's own code so it never leaves the machine.

**Missing C99 pieces**, supplied by hand-written wrappers in
`compat/include/fixed/` that reach the real header with `#include_next`:

| Missing | Where it bit | Repair |
|---|---|---|
| `FP_NAN`, `FP_INFINITE`, `FP_ZERO`, `FP_SUBNORMAL`, `FP_NORMAL` | libstdc++'s C++17 `to_chars` | tags for `__builtin_fpclassify`, which does the work itself -- they only have to be distinct |
| `ENOTSUP` | `std::filesystem` error paths | aliased to `EOPNOTSUPP`, which POSIX explicitly permits |
| `fsblkcnt_t`, `fsfilcnt_t` | `std::filesystem::space` | `unsigned long`, which is what AMIX's own `struct statvfs` fields are |

`<sys/statvfs.h>` also uses `u_long` without including `<sys/types.h>`, so it
compiles only if the translation unit happened to include that first; the
wrapper fixes the ordering trap while it is there.

**Configuration that assumes a modern target.** `std::filesystem` and
`std::stacktrace` are the parts of a hosted libstdc++ that reach hardest into
the operating system, and neither is anything a C++03 program wants.
`--disable-libstdcxx-filesystem-ts`, `--disable-libstdcxx-pch` and
`--disable-libstdcxx-backtrace` remove the parts that were still failing --
libbacktrace, given a target it could not classify, worked down its fallback
chain and settled on the **Windows** implementation, `GetModuleFileNameA` and
all.

**Telling libstdc++ what the target has.** The empty `m68k-*-elf*` case that got
the freestanding build past crossconfig is not enough for a hosted one:
libstdc++ then believes the target has no float maths and compiles its own
`fabsf`, which collides with AMIX's real one. The case now declares what the
target really has, and
`toolchain/patch-libstdcxx-configure.py` derives that list from the target's own
`<math.h>` rather than from anything typed by hand -- twenty functions, with the
`long double` variants deliberately left undeclared, because SVR4.0 genuinely
has none and libstdc++ *should* build its fallbacks for those.

## 15. No static constructor ran, and why nothing said so

With the library complete, OpenTTD compiled, linked and then died in
`std::_Rb_tree_increment` -- walking a `std::map` iterator -- with its own crash
log reporting `Blitter: none` and `Video driver: none`.

Those two lines are the clue. OpenTTD registers every driver and blitter by
constructing a global object whose constructor inserts into a static
`std::map`. If no constructor runs, the maps are never constructed either, and
the first iteration walks a tree of zeroes. A three-line probe confirmed it:

```
3 main: constructed=0 (want 1)      # and not one constructor line above it
```

**The cause is a disagreement about a section name, thirty years apart.**
AMIX's `crt1.o` calls `_init`, `crti.o` provides it, and GCC's `crtbegin.o`
contributes an `.init` fragment that walks `.ctors` -- all correct, and all
verifiable in the disassembly. But the modern linker script folds `.ctors` from
ordinary objects *into* `.init_array`:

```
KEEP (*(.init_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .ctors))
```

so the constructors land in `.init_array`, `.ctors` keeps only crtbegin's `-1`
and crtend's `0`, and `__do_global_ctors_aux` walks an empty list and returns.
On a system whose startup code knows about `.init_array` nobody notices,
because it walks that array too. AMIX's `crt1.o` is from 1991 and has never
heard of it.

Nothing anywhere reports this. It compiles, it links, it runs, and every global
object is quietly left zeroed.

`compat/amix_init.c` contributes its own `.init` fragment that walks
`__init_array_start .. __init_array_end`, and a matching `.fini` fragment for
the destructors. It is a **startfile** named in the specs rather than a member
of `libamixc99`, because a library member is pulled in only when something
references it and nothing ever references a constructor. `tools/amix-cc.sh`
adds it explicitly too: that wrapper passes `-nostdlib` and names every
startfile itself, so the specs' copy of the rule does not reach it.

After that, all four steps of the probe pass on the machine -- constructors
before `main`, in order, destructor after -- and OpenTTD 1.0.5 starts:

```
$ ./openttd -h
OpenTTD 1.0.5

Command line options:
  -v drv              = Set video driver (see below)
  ...
exit=0
```

## Conclusion

The toolchain is not a research risk, it is a build task. The ELF format, the
ABI, the startup path and the floating-point unit have all been checked against
the real machine, and the only mismatch found has a one-line fix. What remains
unproven is C++ specifically: `new`/`delete`, virtual dispatch, templates and —
the one genuine unknown — whether `try`/`catch` unwinds. That is what the next
probe answers.

## -malign-int is the ABI, and libc's __iob is the tripwire

This one cost an evening. It is written out at length because nothing about the
symptom points at the cause, and because the next person to port anything to
this system will meet it.

**The rule.** AMIX's `cc` aligns int and pointer members to 4 bytes and rounds
a struct to its widest member's alignment. m68k GCC rounds to 2. Measured with
AMIX's own compiler, and kept as `probe/abiprobe.c` so a compiler can be
checked against it rather than assumed:

| | AMIX cc | GCC, no `-malign-int` |
|---|---|---|
| `sizeof(FILE)` | **16** | 14 |
| `struct mixed { char a; short b; char c; int d; }` | 12, `d` at 8 | 10, `d` at 6 |
| `struct twoptr { char a; char *p; }` | 8, `p` at 4 | 6, `p` at 2 |
| `struct justchars { char a, b, c; }` | 3 | 3 |

The last row is the one that makes this a rule rather than a coincidence: an
all-char struct is **not** rounded up, so this is natural member alignment with
the struct rounded to its widest member — not a blanket round-to-4, which would
have produced 12 for `mixed` for the wrong reason.

**Why FILE is where it bites.** `/usr/include/stdio.h` defines `stdout` as
`(&__iob[1])`, so every call site does array arithmetic with its own idea of
`sizeof(FILE)`. libc's array is 320 bytes with `_NFILE` 20, a stride of 16. A
GCC-compiled call site computes `__iob + 14` for `stdout` and `__iob + 28` for
`stderr` — two bytes out and four bytes out, into the middle of the previous
FILE.

**What that looks like, which is the part worth remembering.** Not a crash:

- `printf` **works.** GCC turns it into `puts`, and libc indexes its own array
  correctly. So the program's help text prints perfectly.
- `fprintf(stdout, ...)` becomes `fwrite` on a FILE two bytes short of the real
  one. The flags it reads say "not writable", so it returns having written
  nothing, successfully.
- `fprintf(stderr, ...)` is four bytes out, so `_base` and `_ptr` are read from
  the middle of `__iob[1]`. Writing through those pointers stops the process
  and takes its login shell with it.

So a program prints its usage and then every diagnostic it would ever emit
disappears, and it exits 1 with zero bytes on both streams. OpenTTD did exactly
that for hours: `-h` printed 2300 bytes, and every other invocation died
silently. A redirected run leaves the file at **zero bytes**, which reads like
"stderr does not work" and is really "the write went somewhere else".

**How it was found**, since the route matters more than the answer:

1. `-h` works, everything else exits 1 with no output — so it is not the
   arguments, and stdout demonstrably works.
2. A probe bracketing each stdio call with `write(1, ...)` — a syscall, so it
   cannot buffer, cannot be reordered by a FILE, and does not depend on the
   thing under test. The last marker printed names the failing call. This is
   what turned "silent" into "`fprintf(stdout)` returns without writing and
   `fprintf(stderr)` hangs".
3. A native control on the machine: the same program built with AMIX's `cc`
   works completely. That moved the fault off the system and onto our binary.
4. `sizeof(FILE)` against `__iob`'s 320 bytes.

**The fix** is `-malign-int`, installed in the **specs** as a `self_spec`:

    replace("self_spec", "%{!mno-align-int:-malign-int}")

`self_spec` rather than `cc1`, because it rewrites the command line for every
language, so C, C++ and libgcc's own compiles get it from one place. It cannot
be a `CFLAGS` entry: one translation unit compiled without it disagrees with
every other about where a shared struct's fields are, and nothing says so at
link time. It is guarded on `-mno-align-int` so the fault can still be
reproduced deliberately.

**It is not enough to recompile the program.** libgcc and libstdc++ built
before this change are built to the other ABI, and any struct crossing that
boundary is laid out two ways — silently wrong rather than loudly hung. The
whole toolchain has to be rebuilt. `toolchain/build-cross.sh` already installs
the specs before `all-target-libgcc`, so a clean run gets this right.

### Still open: fprintf and `ll`

`compat/amix_compat.c` supplies `snprintf` and `vsnprintf`, which render `ll`
by hand because libc's 1991 formatter predates C99 and goes four bytes out of
step on a `long long`. It does **not** supply `fprintf`, so `fprintf` reaches
that formatter unpatched — and OpenTTD's `OTTD_PRINTF64` and
`OTTD_PRINTFHEX64` expand to `ll` forms because money is `int64`.

This has not bitten yet only because stdio was broken outright. It will bite
the moment something prints money through `DEBUG` or `usererror`. Recorded
here rather than fixed because the fix belongs with a measurement of what
actually breaks, and because a garbled number is a much friendlier failure than
the one above.

## Linking the 1991 libX11.a produces dynamic relocations that point nowhere

Unfinished, but far enough along to be worth writing down: **any binary that
links `libX11.a` carries hundreds of dynamic relocations whose offsets fall
outside the loaded image.**

    binary                       R_68K_32   COPY   JMP_SLOT
    no Xlib                             0      0          5
    references XOpenDisplay           856      1         62
    OpenTTD                          1308      3        115

The 856 have offsets from `0x0a` to `0x1f84` -- section-relative values that
were never rebased to a virtual address, in an image that starts at
`0x80000000`. Applying them writes into unmapped memory; ignoring them leaves
the data words they were meant to fill as zero.

Both failure modes are observed, and they explain two crashes that looked
unrelated:

- `probe/xref`, which merely takes the address of `XOpenDisplay` and never
  calls it, **segfaults before `main`** -- before its first `write(1, ...)`
  marker, which is a syscall and cannot buffer.
- OpenTTD starts, loads its graphics, generates a map, and dies at its first
  Xlib call with `Signal: Illegal Instruction` and **`pc=0x00000000`** -- a
  jump through a pointer that was never relocated.

**It is the modern linker that drops them.** A sibling AMIX port built one set
of GCC 14 objects twice against these same unrepaired archives, changing only
the linker:

| | ld 2.8.1 | modern ld |
|---|---|---|
| dynamic `R_68K_32` | 0 | 7453 |
| `.data` words holding a `0x800xxxxx` address | 66 | 1 |
| `.data` zero words | 963 | 2961 |

binutils 2.8.1 applies these relocations correctly. So the archives are
malformed by the letter of the ELF spec -- `sh_link` on a SHT_RELA section must
name the symbol table -- and the contemporary linker tolerated it anyway, which
is why thirty years of AMIX X clients have worked and why nobody had seen this.
The damage is GNU ld 2.43 disagreeing **silently**.

That comparison is better evidence than mine was, and the reason is worth
keeping: **a relocation count alone cannot settle it.** Zero dynamic
relocations is equally consistent with "resolved statically" and with "dropped
on the floor" -- which is exactly what makes this bug invisible. My own check
was 856 going to 0, and by that argument it was necessary but not sufficient.
What settles it here is the `.data` column above, and on this side the probe
that ran on the machine and printed `screen 0: 800x600 depth 16`. A repair
verified only by a number going to zero would not have been verified at all.

**A third route exists**, if the repair ever becomes inconvenient: compile with
GCC 14 at `--with-cpu=68060` and link with 2.8.1, where the defect never
arises. Not taken here, because this port needs C++ exception unwinding through
that link and whether it survives is an open question -- but it is the reason
the period toolchain was never exposed to this defect.

**The defect is universal, not merely common.** Of `libX11.a`'s 255 members,
236 contain `.rela` sections and **all 236 are wrong** -- 239 sections in
total. The remaining 19 are not exceptions; they simply carry no relocations.
That is the form the count belongs in: "236 of 255" invites the reader to ask
what is special about the other nineteen, and the answer is nothing. The
figures were confirmed independently against a read-only pristine AMIX mount,
by someone who went looking to disprove them.

`-lX11` alone accounts for all 856; `-lsocket` and `-lnsl` add none. The
symbols involved are libc *data*: `errno` (81 references), `__ctype` (61),
`_cleanup` (50), `getenv` (42), `_end` (37), `_etext` (29). In the SVR4 model
those need the executable to own a copy -- which is exactly what the `data.o`
member of `/usr/ccs/lib/libc.so` provides for `__iob`, and why stdio works at
all here. There is one COPY relocation (`errno`) where there should presumably
be several, and 856 per-reference relocations where there should be none.

**The lead to follow next**: `/usr/ccs/lib/libc.so` is an ar archive whose
members include `_ctype.o`, `errlst.o` and `data.o`. If the link pulls `data.o`
(for `__iob`) but not the members defining `__ctype` and friends, those symbols
stay dynamic and every reference to them becomes a relocation instead of being
bound to a local copy. Forcing those members in, or linking the matching parts
of `libc.a`, is the first thing to try.

### Two of my own measurements were wrong before this one was right

Worth recording because both produced confident nonsense:

- A control that linked `-lX11` and never referenced it proved nothing: a
  static archive yields only the members something refers to, so the binary was
  byte-identical to one linked without Xlib. Referencing a symbol through a
  function pointer is what actually pulls the library in -- 14 kB became 72 kB.
- `awk`'s `strtonum` does not exist in mawk. A range check written with it
  silently evaluated every address as 0, which turned "all offsets are below
  0x100" into a finding. The real range is 0x0a to 0x1f84, and the conclusion
  survived only because it was re-derived in Python.
