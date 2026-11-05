# Python 3 for Amiga UNIX

An experimental **CPython 3.11.18** port for updated Amiga UNIX (AMIX),
cross-built with our AMIX-adapted **GCC 14.2** toolchain and **`-m68060`**.
Thread support comes from **GNU Pth’s pthread emulation library**.

Python now runs on the physical 68060 machine used for development. It has
passed tests of four Python threads, queues, thread-local data, Unicode,
JSON, file I/O, compression, hashes and other standard-library modules.

This repository contains the **source recipe, patches, private C adapters
and tests**. Builders fetch CPython upstream and supply their own AMIX
sysroot. Compiled interpreters, system files and VM images are excluded.

## Current status

The tested machine reports `68060-261027-ia`. A clean build reports
`[GCC 14.2.0]`. All target objects use the GCC 14 cross-compiler with 68060
optimizations. Python's installation prefix is `/usr/local`; GNU Pth
(`library/pth`) is the shared `libpthread.so.20`/`libpth.so.20` in
`/usr/local/gnu/lib`.

| Area | Physical test result |
| --- | --- |
| Interpreter | Big integers, float rounding, Unicode and JSON passed |
| Threads | Four workers, bounded queue, thread-local data, joins and timed wait passed |
| I/O | UTF-8 file contents and pipe/select operations passed |
| Compression | zlib, gzip and ZIP round-trips passed |
| Hashes | Known MD5, SHA-1, SHA-256 and SHA3-256 vectors passed |
| Other modules | struct, datetime and selected math/environment checks passed |
| Generated code | Passed the toolchain’s 68060 instruction check |

Since 2026-11-05 every stdlib extension module that AMIX can support is built
into the interpreter (`Modules/Setup.stdlib`, static; `lib-dynload` stays
empty): `_socket`, `mmap`, `_ssl` and `_hashlib` (OpenSSL 3.5), `_bz2`,
`pyexpat`/`_elementtree`, `_posixsubprocess`, `fcntl`, `select`, `termios`,
`resource`, `syslog`, `unicodedata`, `_decimal`, the CJK codecs and the rest
of the standard set. GNU Pth, OpenSSL, zlib, bzip2 and expat are the shared
libraries of their own components, found through the runpath
`/usr/local/lib:/usr/local/gnu/lib`. `venv` is installed; `ensurepip` is not:
pip and setuptools are packages of their own (see below).

Not available:

| Module | Why |
| --- | --- |
| `ctypes` | Needs libffi. libffi has an m68k port and `ffi_call` looks portable, but callbacks (closures) write trampolines that need an instruction-cache flush, and AMIX offers no user-mode flush on the 68060 (copyback cache). Needs a `library/libffi` component and a kernel call first. |
| `sqlite3` | No `library/sqlite` component yet; the amalgamation is the easy next step. |
| `_lzma`, `_uuid`, `readline`, `_curses`, `_dbm`/`_gdbm`, `_tkinter` | No AMIX library component (curses: Python 3.11 builds it only through setup.py). |
| `_multiprocessing`, `_posixshmem` | No POSIX semaphores or shm_open in AMIX libc. |
| IPv6 | Built `--disable-ipv6`; `inet_pton`/`inet_ntop` are IPv4 adapters. |

Sockets connect blocking on AMIX (see the port notes): a timeout still
applies to sending and receiving, but not to the connect itself.

See [the port notes](docs/PORT-NOTES.md) for exact scope, source checksum,
executable hash, compatibility changes and remaining limits.

## Build on Linux

Use GNU build tools, curl, tar/xz, Python 3, the AMIX sysroot with the
components of `python3.manifest` installed (`make requires` builds what is
missing), and the AMIX-adapted GCC 14 toolchain. A generic m68k compiler is
insufficient. The tested toolchain is
[asokero/gcc14-cross-amix](https://github.com/asokero/gcc14-cross-amix).

From this component's directory:

```sh
bash tools/build-python-amix.sh
python3 tools/stage-python-install.py
```

The build script unpacks the pinned CPython archive afresh, applies
`tools/patch-python-amix.py` and `patches/*.patch`, configures against the
sysroot through `tools/amix-pkg-config`, and cross-builds the interpreter
with every module linked in. A matching Linux-native interpreter is built
only to generate cross-build files; it is never deployed to AMIX. The stage
script writes `dist/python3-amix-install.tar.gz` (private: binaries are not
published).

## Install on AMIX

Every step on the A3000 goes through `tools/amix-box` (one session at a
time, never while a player runs). Keep 200 MB free on `/`.

A first installation: extract the archive in a fresh directory and run
`sh install.sh` as root; it refuses to replace differing files.

An update of an installed Python: extract the archive in a directory on the
root file system (so every step is a rename), for example
`/usr/local/.python3-update`, then as root:

```sh
sh update-installed.sh prepare    # each part beside the installed one as .new
sh update-installed.sh verify     # sha256 of every .new file, with python3
sh update-installed.sh swap       # .new in, the old copies kept as .old
sh update-installed.sh clean      # after testing: remove the .old copies
```

`sh update-installed.sh rollback` puts the `.old` copies back. Back up the
installed Python first.

| Directory | Contents |
| --- | --- |
| `/usr/local/bin` | `python3.11`, `python3`, and their config helpers |
| `/usr/local/lib/python3.11` | Standard library, `venv`, sysconfig data and static SDK support |
| `/usr/local/lib` | `libpython3.11.a` |
| `/usr/local/include/python3.11` | Python headers, target pyconfig and private AMIX compatibility headers |
| `/usr/local/man/man1` | `python3.11.1` and `python3.1` |
| `/usr/local/share/doc/python3-amix` | Licence, notes, tests, receipt and installation manifests |

For GCC 14 embedding flags, use `python3.11-config --cflags` and
`python3.11-config --embed --ldflags` (static `libpython3.11.a` and
`libmpdec.a` on the shared Pth, OpenSSL, zlib, bzip2 and expat).

## pip and Python packages

pip and setuptools are components of their own, built from their upstream
`py3-none-any` wheels with `make-rules/python.mk`:
[`library/python/pip`](../../library/python/pip) and
[`library/python/setuptools`](../../library/python/setuptools). Further Python
modules for the system belong in `components/library/python/<name>` the same
way, never pip-installed by hand into `/usr/local/lib/python3.11/site-packages`.

`library/python/pip` installs `/usr/local/pip.conf` with `user = true`: the
system Python's pip installs into the user's own site
(`~/.local/lib/python3.11/site-packages`, scripts in `~/.local/bin`). A venv
(`python3 -m venv --without-pip --system-site-packages DIR`, which uses the system
pip) has its own prefix and installs into the venv.

```sh
pip install six                   # into ~/.local
```

Tested on the A3000 (2026-11-05): `pip install six` from PyPI into root's
user site, 13.6 min wall clock the first time (pip's byte code is compiled
then); `pip uninstall -y six` afterwards 4.3 min. pip is slow on a 68060:
most of the time is importing pip itself. A venv made with
`--without-pip --system-site-packages` runs the system pip.

What works:

- **Pure-Python wheels** (`py3-none-any`): they are the same files on every
  machine. pip downloads them from PyPI over TLS (pip's own CA bundle).
- **Pure-Python sdists**: pip builds them in an isolated environment, which
  downloads setuptools for the build; slow on a 68060, but no compiler needed.

What does not:

- **Packages with C extensions.** PyPI has no wheels for AMIX/m68k, so pip
  falls back to the sdist and needs a C compiler. `sysconfig` names the GCC 14
  cross compiler (`m68k-elf-gcc`), which is not on the box, and the box's
  native gcc 2.7 cannot build against CPython 3.11's headers. This interpreter
  links every stdlib module in and has never loaded an extension module
  (`lib-dynload` is empty). Such packages need a component that cross-builds
  them on the PC.
- `ctypes`-based packages, see above.

## Sources and licences

CPython is fetched from the [official 3.11.18 release](https://www.python.org/downloads/release/python-31118/).
CPython-derived patches and math adapters retain its
[PSF licence](ports/python/LICENSE.PSF). Original adapters and tests use the
[MIT licence](LICENSE). GNU Pth is an external dependency with its own LGPL terms.

The [source patch](ports/python/cpython-3.11-amix.patch) is available for review
and manual builds. The normal recipe applies the equivalent Python helper;
apply one method to a fresh source tree.
