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
optimizations. The interpreter is staged in a scratch directory.

| Area | Physical test result |
| --- | --- |
| Interpreter | Big integers, float rounding, Unicode and JSON passed |
| Threads | Four workers, bounded queue, thread-local data, joins and timed wait passed |
| I/O | UTF-8 file contents and pipe/select operations passed |
| Compression | zlib, gzip and ZIP round-trips passed |
| Hashes | Known MD5, SHA-1, SHA-256 and SHA3-256 vectors passed |
| Other modules | struct, datetime and selected math/environment checks passed |
| Generated code | Passed the toolchain’s 68060 instruction check |

One machine prerequisite remains: `/dev/urandom` currently returns
`No such device`. Normal Python startup cannot initialize its random hash
seed. Controlled tests explicitly used **`PYTHONHASHSEED=0`**; this is not
built into Python as a default. The owner will rebuild the kernel with the
random driver separately, after this port. `os.urandom()` needs that device.

The current scope is a scripting and threading proof. Socket/SSL, ctypes,
SQLite, subprocess extensions, pip and broad package compatibility remain
future work. GNU Pth is cooperative: CPU-bound threads need to yield or wait,
and there is no parallel CPU speedup on one 68060.

See [the port notes](docs/PORT-NOTES.md) for exact scope, source checksum,
executable hash, compatibility changes and remaining limits.

## Build on Linux

Use GNU build tools, curl, tar/xz, Python 3, your AMIX sysroot, and the
AMIX-adapted GCC 14 toolchain. A generic m68k compiler is insufficient.

The tested toolchain is [asokero/gcc14-cross-amix](https://github.com/asokero/gcc14-cross-amix)
at revision `58130fe7a322a19cb4c010a8257444cf1c016a07`.
First build [GNU Pth for AMIX](https://github.com/isoriano1968/gnu-pth-amix).

From this repository directory:

```sh
export CC="$HOME/opt/m68k-amix/bin/m68k-elf-gcc"
export PTH_PREFIX="$HOME/opt/pth-amix"
PYTHON_EXTRA_MODULES=yes bash tools/build-python-amix.sh
python3 tools/stage-python-proof.py
```

The script downloads and verifies the pinned CPython archive, applies the
AMIX fixes and cross-builds Python. A matching Linux-native interpreter is
built only to generate cross-build files; it is never deployed to AMIX.

Extra modules need the previously cross-built zlib 1.3.1 tree at
`~/src/upstream/zlib-1.3.1`, or the path set in `ZLIB_SOURCE`.

## Try it on AMIX

Use a dedicated scratch directory. Transfer the target executable from
`~/build/python311-amix/python` and `dist/python311-amix-stdlib.tar.gz`.
Keep the original executable for debugging, strip a copy for transfer, and
run the toolchain’s 68060 instruction checker before deployment.

Extract the library archive and place the executable at `py311/bin/python3.11`.
While the random device is unavailable, run from the scratch directory:

```sh
PYTHONHASHSEED=0 PYTHONHOME="$PWD/py311" py311/bin/python3.11 -S py311/smoke.py
```

Transfer and run `tests/python3-stdlib.py` separately for the extra-module
tests. Bound hardware experiments with the cross-built `probes/bounded-run.c`.
The recipe does not install Python or change kernel/boot files. The intended
installation prefix is `/usr/local/gnu`.

## Sources and licences

CPython is fetched from the [official 3.11.18 release](https://www.python.org/downloads/release/python-31118/).
CPython-derived patches and math adapters retain its
[PSF licence](ports/python/LICENSE.PSF). Original adapters and tests use the
[MIT licence](LICENSE). GNU Pth is an external dependency with its own LGPL terms.

The [source patch](ports/python/cpython-3.11-amix.patch) is available for review
and manual builds. The normal recipe applies the equivalent Python helper;
apply one method to a fresh source tree.
