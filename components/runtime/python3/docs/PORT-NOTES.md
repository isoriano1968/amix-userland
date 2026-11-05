# Experimental Python 3.11 for AMIX

CPython 3.11.18 has been cross-built with the AMIX-adapted GCC 14.2 compiler
and run on the physical 68060 machine. Its threading backend links GNU Pth's
`libpthread.a`. The custom historical pthread runtime is not linked.

The initial scripting and threading proof was staged under
`/tmp/pthreads-amix-261028/python/py311`. Installation now uses `/usr/local`
through `install.sh`; GNU Pth remains under `/usr/local/gnu`.
The tested kernel reports `68060-261027-ia`.

## Physical results, 2026-10-28

Both the initial core interpreter and the later clean build passed:

- Arbitrary-size integer arithmetic and Python's ties-to-even float rounding.
- Unicode strings, JSON round-trips, and UTF-8 file contents.
- Four Python threads using a bounded queue, with 160 distinct work items.
- Thread-local values, thread joins, a timed Event wait, and a reentrant lock.
- Monotonic timing and conversion of Unix epoch zero to a calendar date.

The clean build also passed tests of zlib, gzip and ZIP round-trips on a
262144-byte input; known MD5, SHA-1, SHA-256 and SHA3-256 vectors; binary
struct packing; datetime; selected math edge cases; pipe/select I/O; and
environment updates. These are focused acceptance tests, not the complete
CPython regression suite.

The executable reported:

```text
3.11.18 (main, Oct 28 2026, 14:59:57) [GCC 14.2.0]
PYTHON3_AMIX_SMOKE_PASS
PYTHON3_AMIX_STDLIB_PASS
```

SHA-256 of the tested stripped clean-build executable:

```text
5b0e4d81237f3ac74123cab5f46a7bb6d2dc3652fb6256e680c32646614c7b72
```

The toolchain's encoding-based instruction check inspected 1005 long
multiply/divide instructions and found no forms removed by the 68060.
The link map records the GNU `libpthread.a` archive. The standalone recipe
and the subsequent `/usr/local` build passed the physical Python test scripts.
The hash above identifies the installed `/usr/local/bin/python3.11` executable.

## All stdlib modules, sockets and TLS, 2026-11-05

The interpreter now links every stdlib module AMIX can support:
`patches/0002` makes configure use `Modules/Setup.stdlib` for AmigaUNIX (3.11
does that only for Emscripten/WASI and runs setup.py otherwise) and the recipe
sets `MODULE_BUILDTYPE=static`. Libraries come from the sysroot through
`tools/amix-pkg-config`, all shared: Pth (`/usr/local/gnu/lib`), OpenSSL
3.5.4, zlib, bzip2, expat (`/usr/local/lib`); the runpath is
`/usr/local/lib:/usr/local/gnu/lib` and `tools/amix-fix-needed` puts the
NEEDED names first (libnsl dlopens for its lookups). The stripped
interpreter is 7.4 MB (3.6 MB before).

What it took:

- AMIX's `sys/socket.h`, `sys/mman.h` and `syslog.h` declare no functions.
  `amix-python.h` declares them (before Pth's `pthread.h`, which then maps
  connect/accept/recv/send to its yielding calls), plus `gethostname`
  (libnsl), `INET_ADDRSTRLEN` and `_SC_PAGE_SIZE`.
- configure's socket function tests compile against those headers and fail,
  and since 3.11 (for WASI) `socketmodule.c` leaves out `socket()`, `bind()`,
  `connect()`... without `HAVE_SOCKET` etc. The recipe answers
  `ac_cv_func_socket=yes` and the rest; the functions are in libsocket.
- `ports/python/amix-inet.c`: IPv4 `inet_pton`/`inet_ntop` for CPython's
  getaddrinfo/getnameinfo fallbacks (AMIX has `inet_addr`/`inet_ntoa` only).
- `patches/0004` with `ports/python/amix-connect.c`: AMIX's sockets (SVR4.0
  sockmod) report a non-blocking connect done, with `SO_ERROR` 0, before it
  is; the first send then fails. Pth's connect wrapper waits the same way.
  Python connects blocking, with `O_NONBLOCK` cleared around libsocket's
  connect, as curl and libupnp do on AMIX. The kernel's connect timeout
  applies, not the socket's; other Pth threads wait meanwhile.
- `patches/0001`: OpenSSL is built `no-threads` and without Pth's
  `pthread.h`, so its blocking calls never yield and no two Pth threads are
  in OpenSSL at once; `_ssl`/`_hashlib` accept it on AMIX.
- `patches/0003`: `_bz2` used `uint32_t *` for bzip2's `unsigned int`
  counters (`uint32_t` is unsigned long here), as the zlib module did.
- `patches/0005`: AMIX libc's malloc aligns to 4, not 8. CPython's parser
  arena then started a block's data 4 bytes in, and a one-off block of
  exactly the requested size was overrun by 4 bytes. Compiling
  `html/entities.py` (pip imports it) corrupted the heap and crashed in libc
  `malloc`; `PYTHONMALLOC=debug` showed the overrun. `ab_mem` is now aligned
  with slack.
- The compat objects are an archive, `libamixpythoncompat.a`: as objects in
  `LIBS`, configure's `-lsocket` test linked them twice and failed.

Tested on the A3000 with the new binary against the installed stdlib: all
modules import; OpenSSL 3.5.4; anonymous and file `mmap`; subprocess with
pipes; bz2, decimal; `getaddrinfo("pypi.org")` 5.6 s (libnsl's resolver);
`create_connection` 0.1 s; TLS 1.3 handshake with pypi.org 2.5 s and a
verified HEAD; `urlopen` of a 16 KB HTTPS page 34 s; socketpair; loopback
listen/accept/connect. A cold first run that compiles the stdlib byte code
took minutes; after that `python3 -c pass` takes about 5 s, most of it
loading the shared libraries (libcrypto.so.3 is 6 MB).

Not built: `ctypes` (libffi closures need an instruction-cache flush AMIX
does not offer user programs on the 68060), `sqlite3` (no component yet),
`_lzma`, `_uuid`, `readline`, `_curses` (setup.py only in 3.11), `_dbm`,
`_tkinter`, `_multiprocessing`/`_posixshmem` (no POSIX semaphores or shm).

## Random-device prerequisite

Normal interpreter startup currently fails when obtaining its random hash
seed. The physical `/dev/urandom` node exists, but a native `dd` open returns
`No such device`. The Python tests above explicitly set `PYTHONHASHSEED=0`.
This is a controlled proof setting; it is not made the interpreter's default.
`os.urandom()` remains unavailable until the device is functional.

The user's driver source and object exist at `/usr/sys/amiga/driver/random.c`
and `random.o`, and the driver Makefile includes that object. The source
`/usr/sys/master.d/kernel.c` registers its entry points at major 48, matching
the device nodes. The older `/unix` contains `randopen`; the newer
`/stand/unix.z3` has no `randopen` or `randread` symbol. The owner confirmed
on 2026-10-28 that `/stand/unix.z3` is the currently booted kernel. Together
with the device-open failure, this supports the diagnosis that the random
driver is missing from the running kernel despite its presence in the older
`/unix` image and source tree.
The source describes a compatibility PRNG without modern entropy accounting.
Enabling the interface and establishing cryptographic suitability are separate
tasks. The owner deferred kernel integration and will build the kernel
separately after the Python port. No boot or kernel files were changed for
this proof.

## Reproduce the cross-build

The sysroot needs the components in `python3.manifest` (`make requires`
builds and installs what is missing). Use the pinned, AMIX-adapted GCC 14
toolchain. From the component directory on Linux:

```sh
bash tools/build-python-amix.sh
```

The script fetches the official CPython archive and verifies its SHA-256:

```text
9120e0a653868f6e3698d36cdd99e5a58903ec9469e448f21d98f7b16d8fa290
```

Source: [Python 3.11.18](https://www.python.org/downloads/release/python-31118/).

Every run unpacks a fresh tree and applies `tools/patch-python-amix.py`, then
`patches/*.patch`. A matching Linux-native interpreter is built only to
generate files needed for cross-compilation. Every AMIX target object uses
GCC 14.2 with `-O2 -m68060 -fno-strict-aliasing`; CPython uses GNU C11 mode
and 1 MiB default worker stacks. Configure uses `--prefix=/usr/local`.
`PYTHON_SKIP_CONFIGURE=yes` reuses an already configured tree (no fresh
unpack) for incremental work. Paths can be set through `PYTHON_UPSTREAM`,
`PYTHON_SOURCE`, `PYTHON_BUILD`, `BUILD_PYTHON` and `AMIX_SYSROOT`.

Output is `~/build/python311-amix/python`, unstripped; the stage script
strips the installed copy and runs the 68060 instruction checker on it.
`tests/python3-smoke.py` and `tests/python3-stdlib.py` are the acceptance
tests; bound hardware experiments with `probes/bounded-run.c`.

## Installation and SDK

After a build configured for `/usr/local`, run
`PYTHON_BUILD=/absolute/build/path python3 tools/stage-python-install.py`.
Transfer the resulting private `dist/python3-amix-install.tar.gz`, extract it
in a fresh AMIX directory, and run `sh install.sh` as root. The installer uses
the file/link manifests and byte comparisons; it refuses differing existing
files. It does not invoke the obsolete package manager.

The payload includes the stripped interpreter, pure Python library and
resources, target sysconfig data, public/internal headers, the static Python
archive, private compatibility/zlib archives, config helper, manual page,
licence, receipt and tests. It creates `python3`/config/manual aliases and
empty `site-packages`/`lib-dynload` directories. GUI, regression-test,
ensurepip trees remain outside the distribution (pip is library/python/pip);
venv is included since 2026-11-05.

Installed paths are listed in
`/usr/local/share/doc/python3-amix/INSTALL-FILES` and `INSTALL-LINKS`.
`python3.11-config --embed --ldflags` includes the GNU Pth dependency under
`/usr/local/gnu`; Python's own files use `/usr/local`.

The stage script requires a fresh staging directory; set `PYTHON_STAGE` to
another empty path when preparing a new payload. Installation does not
silently set a hash seed or change shell profiles. The current machine still
needs explicit `PYTHONHASHSEED=0` until its kernel random device is enabled.

Physical installation verified 859 regular files and four aliases. With
the final native-tool compatibility fixes, repeating the complete
`install.sh` also verified all files and aliases and exited 0. With
`PYTHONHOME` and `PYTHONPATH` unset, the installed interpreter passed
`tests/python3-installed.py`, the thread/Unicode/JSON smoke test, and the
compression/hash/struct/datetime/math/select/environment test. A GCC 14
embedding program built through the staged config helper ran successfully
against the installed library tree and reported `/usr/local`.

The config-helper alias is a hard link: this kernel cannot execute the
relative symlink to the shell script correctly. The interpreter, manual and
archive aliases are symbolic links. Native-tool compatibility also requires
attached `xargs -n16` syntax and an existence check before `mkdir -p`.

## Port changes

`tools/patch-python-amix.py` applies the source changes; the equivalent diff
is [ports/python/cpython-3.11-amix.patch](../ports/python/cpython-3.11-amix.patch).
Apply one approach to a fresh tree, not both.

- Recognize AMIX in the two cross-platform configure checks.
- Use elapsed `times()` ticks for monotonic time. The measured resolution is
  1/60 second; the adapter tracks 32-bit counter wrap between observations.
- Provide a bounded tty-name buffer without the unavailable `_SC_TTY_NAME_MAX`.
- Use zlib's actual `uInt` type for its output-count pointers. On this GCC
  target, `uint32_t` is unsigned long while zlib's `uInt` is unsigned int.

Private adapters in `ports/python/` supply missing wide-string operations,
math functions, environment updates, time-result copies, and declarations for
existing libc calls. They are linked into Python and never installed over
vendor headers or libraries. Python uses its own UTF-8 locale conversion
path; AMIX libc's locale is unchanged.

The time/environment adapters rely on cooperative execution: no scheduling
point occurs while copying static libc results or updating the environment.
They have not been validated with preemptive kernel threads.

## Current limits

GNU Pth remains cooperative. CPU-bound Python threads need explicit yielding
or suitable waiting/I/O operations; this does not provide parallel CPU work.
Signal handling, fork with live threads, extension loading and the complete
cancellation/timeout surface have not passed a full audit.

Extension modules are all built in; loading one from `lib-dynload` has never
been tried, so pip cannot install packages with C extensions (see README).
ctypes and sqlite3 are missing, see above.

The configured `off_t` and `time_t` types are 32 bits. Large-file and
post-2038 behavior are outside this acceptance milestone.

The private math fallbacks passed focused hardware checks. A Linux comparison
sampled 4000 inputs each for expm1/log1p/log2/exp2 with a maximum observed
difference of one ulp, plus bit-exact nextafter cases. This is not a complete
libm accuracy or floating-exception conformance result.

Raw development evidence is in ignored `local/logs/python-linux.log` and
`python-amix.log`; compiler commands are displayed in visible terminals.
Kernel sources, licensed sysroot files and generated binaries remain outside
Git. CPython-derived changes use the PSF licence in `ports/python/LICENSE.PSF`;
original adapters/tests use the project's MIT licence in `LICENSE`.
