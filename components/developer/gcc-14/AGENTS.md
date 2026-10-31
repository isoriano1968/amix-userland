# AGENTS.md

Guidance for coding agents and for anyone changing this repository. It is short
on purpose; the substance is in [`docs/TOOLCHAIN.md`](docs/TOOLCHAIN.md).

## What this is

A GCC 14 cross compiler targeting **AMIX** — Amiga UNIX, Commodore's SVR4 for
the Amiga, 1991 — with working C++, plus the parts of a C library that SVR4.0
predates. It exists because AMIX's own compiler is GCC 2.7.2.3: no C++ front
end, no `-m68060`, no C99.

The repository holds build scripts, a small compat layer, probes and
measurements. It does not hold GCC, binutils or anything from AMIX; those are
downloaded or supplied by the person building.

## Read the measurements before changing a flag

`docs/TOOLCHAIN.md` records why each setting is what it is. Several are
load-bearing in ways the flag name does not suggest, and changing one usually
produces a **working binary that behaves wrongly** rather than a build error:

- **`-malign-int` is the ABI, not a tuning flag.** AMIX's `cc` rounds a struct
  to 4 bytes; m68k GCC rounds to 2, making `sizeof(FILE)` 14 where libc's array
  strides by 16. It is set in the specs so it cannot be forgotten in one
  translation unit. The symptom when it is missing: `printf` works, `fprintf`
  writes nothing, and the program stops without a message.
- **`--with-cpu=68060` is a correctness setting and must be the default.**
  libgcc's 64-bit divide built for a 68020 uses instructions the 68060 removed;
  without Motorola's ISP in the kernel they trap fatally. `CFLAGS_FOR_TARGET`
  does not reach libgcc's own compile — measured. Code built this way still
  runs on 68020/030/040, because it only ever avoids instructions.
- **The target is `m68k-elf`, not `m68k-linux-gnu`.** The Debian m68k compiler
  looks like a shortcut: its `libgcc_eh.a` asks a dynamic loader
  (`_dl_find_object`) for exception tables and locks with pthreads, and AMIX has
  neither. An `elf` target builds the registry unwinder instead.
- **`export MAKEINFO=true`**, or binutils dies in `doc/bfd.info` minutes in.
- **The C++ linkage wrappers are generated, never committed.** AMIX's C headers
  predate C++ and declare no linkage, so a C++ file including `<stdio.h>` gets a
  mangled `printf`. They are built from whatever the local sysroot actually has.
- **`-z norelro`, and `-U__linux__ -U__gnu_linux__ -Ulinux`.** The code
  generator is a Linux cross compiler; portable sources otherwise take Linux
  paths AMIX cannot satisfy.
- **Do not add `-fpcc-struct-return`.** It moves the struct return value into
  memory but still passes the address in `%a1`, where AMIX wants `%a0`.
- **A pointer result goes back in `%a0` and `%d0`.** That is m68k SVR4, and
  gcc 2.7 code on AMIX (every vendor and R6.3 library) reads `%a0`. Stock
  m68k-elf uses `%d0` only, so a gcc-2 caller of one of our functions got
  garbage. `toolchain/patch-gcc-svr4-return.py` switches `m68kemb.h` to
  linux.h's `m68k_function_value`. The other direction always worked, which is
  why no probe calling libc caught it.
- **`-fno-fold-mem-offsets` is in the specs because GCC 14 miscompiles m68k
  with that pass**: it drops the displacement when folding into indexed
  addressing, so `l[count-1]` stores to `l[count]`. See `install-specs.sh`.
- **The vendor's 1991 X archives need repairing before they can be linked.**
  Every object in `libX11.a` has `sh_link` on its `.rela` section pointing at
  itself; modern `ld` silently drops those relocations and the program jumps to
  address 0 on first use. `tools/fix-x-libraries.sh` writes repaired copies and
  leaves the originals alone.

## Nothing licensed, nothing private

- The **AMIX sysroot** — headers, libraries, `crt*.o`, `libc.so` — is licensed
  material from a real installation. It lives outside the repository, named by
  `AMIX_SYSROOT` in `config.sh`, and must never be committed.
- `config.sh` and `local/secrets.env` are machine-local and gitignored;
  `*.example` files are the committed templates.
- Check what is staged before committing, because `.gitignore` here is partly a
  whitelist and a new file can be invisible:

```sh
git add -A && git status --short
git diff --cached | grep -inE "AMIX_PASS=|password|secret"
```

## Probes, not debugging sessions

`probe/` holds standalone programs that each ask one question and print the
answer, flushed, in an order where a crash names the step it died in. Every
fact in `docs/TOOLCHAIN.md` was established this way. Add a probe rather than
reasoning about what the machine probably does.

Four lessons from the probes that already exist:

- **Run the same battery on the host where the code is portable**
  (`tools/test-compat-host.sh`). The formatter's tests passed natively and died
  on the Amiga, and that difference is what localised the fault.
- **Make a probe prove what you think it proves.** The first 64-bit probe
  divided by the constant 10, which GCC folds into a multiply — so it cleared
  libgcc's divide routine without ever calling it, and sent the hunt the wrong
  way for an hour. If a probe is meant to exercise a library routine, make the
  operands `volatile`.
- **A C probe has to ask for the compat layer by name: `-lamixc99`.**
  `amix-cc.sh` adds it for C++, but the C path links only `-lgcc -lc`, and the
  functions SVR4.0 predates live in the compat library — `usleep` among them.
  The symptom is `undefined reference to 'usleep'` from a file that compiles
  without a warning and whose call links cleanly from C++, which reads as a
  problem in the source rather than in the link line.
- **`amix-cc.sh` passes no `-O`, so a probe is built at `-O0` unless it asks.**
  A probe built that way measures its own loop, not the machine, and it does so
  plausibly enough to be believed. A memory-bandwidth probe here reported
  8.5 MB/s at `-O0` and 13.7 MB/s at `-O2` for the same loop on the same
  hardware; worse, a proposed optimisation looked worthless at `-O0` and 12%
  faster at `-O2`, so the two builds gave opposite answers to the question the
  probe existed to settle. The tell is a probe whose numbers do not line up
  with the optimised program it is supposed to explain: if the probe is slower
  than the real thing, suspect the probe's flags before the machine.

## Running things on a real machine

Optional, and only needed to verify against hardware. `tools/hw.py` runs
commands over telnet and `tools/push-run.sh` moves a binary across by TFTP and
runs it. Both read the address and login from `local/secrets.env`.

Four traps worth knowing before blaming the machine. Each of them answers
without an error, which is what makes them expensive:

- **A command longer than 256 characters is silently dropped**, not run — that
  is the terminal's canonical-mode line limit. It produces no error and the
  command still echoes back, so it looks like a machine that accepted the work
  and hung. `hw.py` splits long commands across a file to avoid it. The tell,
  if you meet it elsewhere: a `cp` on the same line leaves no file behind, and
  no amount of machine slowness explains that.
- **Wait for a process to exit, not for text in its log.** A wait loop grepping
  for a marker matches the marker inside the *echoed* command, long before that
  command has produced anything.
- **This `grep` has no `-E` and no `\|`.** Alternation is a GNU extension, so
  `grep "amiwm\|twm"` matches nothing at all and exits quietly. The output is
  an empty list that looks like an answer: it was read here as "no window
  manager is running" while one was. Use one `grep` per pattern.
- **`awk "{print \$2}"` never reaches awk.** In double quotes the *remote*
  shell expands `$2` — an unset positional parameter — to nothing, leaving the
  program `{print }`, which prints the whole line. So
  ``kill `ps -ef | grep X | awk "{print \$2}"` `` hands `kill` an entire `ps`
  line. Here it answered `invalid id`; the dangerous case is the one where it
  fails silently and the process is assumed killed. Quote the awk program with
  single quotes, or skip awk: `... | while read user pid rest; do kill $pid;
  done`.
