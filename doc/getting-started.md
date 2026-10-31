# Getting started

From a fresh clone to a program running on your Amiga. You build on a Linux PC
and copy the result to the Amiga over the network.

There are two repositories and two compilers, and they do not mix:

| Repository | What is in it | Compiler | Runs as |
| ---------- | ------------- | -------- | ------- |
| amix-userland | Programs and libraries | GCC 14 (`m68k-elf-gcc`) | `~/opt/m68k-amix` |
| [amix-gate](https://github.com/isoriano1968/amix-gate) | Kernel, drivers | GCC 2.7.2.3 (`m68k-cbm-sysv4-gcc`) | `~/opt/amix-cross` |

**Never use GCC 14 for the kernel or a driver.** Only GCC 2.7.2.3 links with
what AMIX itself was built from.

## What you need

- A Linux PC with `gcc`, `g++`, `make`, `python3`, `curl`, `bzip2`, `xz`, `m4`,
  `patch`, `file` and `autotools-dev`, and about 10 GB free.
- An Amiga running AMIX 2.1 on your network.
- **Your own AMIX installation.** Neither repository contains AMIX files, and
  none may ever be added: they are not ours to share.

## 1. Get the repository

```sh
mkdir -p ~/Sources/usr-amix
cd ~/Sources/usr-amix
git clone https://github.com/isoriano1968/amix-userland.git
```

Some components still expect this exact location, so use it unless you are
ready to adjust their paths.

## 2. Make the sysroot

The sysroot is a copy of the parts of AMIX that a cross compiler builds against.
Mount or copy your AMIX disk on the PC, then copy three trees, keeping their
layout:

```sh
AMIX=/path/to/your/mounted/amix
SR=$HOME/opt/m68k-amix-sysroot
mkdir -p $SR/usr/ccs
cp -R $AMIX/usr/include $SR/usr/
cp -R $AMIX/usr/lib $SR/usr/
cp -R $AMIX/usr/ccs/lib $SR/usr/ccs/
```

It must contain:

| Path | What |
| ---- | ---- |
| `usr/include` | The system headers |
| `usr/lib` | `libc.so.1`, `ld.so.1` and the other shared libraries |
| `usr/ccs/lib` | `crt1.o`, `crti.o`, `crtn.o` and `libc.so` |

## 3. Build GCC 14, for userland

```sh
cd ~/Sources/usr-amix/amix-userland/components/developer/gcc-14
cp config.sh.example config.sh
$EDITOR config.sh
toolchain/build-cross.sh
```

In `config.sh`, set `AMIX_SYSROOT` to your sysroot, `$HOME/opt/m68k-amix-sysroot`.
Leave `TC_PREFIX` as `$HOME/opt/m68k-amix`, because the components look there.
The build takes about 25 minutes and needs about 5 GB of scratch space. If your
`/tmp` is small, set `TC_WORK` too.

To build anything that uses X, add the X11 files. This step repairs AMIX's X
libraries too, which the linker otherwise misreads without any warning:

```sh
tools/sync-sysroot.sh $AMIX
tools/fix-x-libraries.sh
```

Check the compiler:

```sh
~/opt/m68k-amix/bin/m68k-elf-gcc --version
```

The component `developer/gcc-14/README.md` has the full background.

## 4. Build your first component

A component is one directory under `components/<category>/`. Most build with
`make build`, and each `Makefile` starts with a line saying what it is.

`amtop`, a small system monitor, is a good first one. It needs ncurses, so build
that and put it in the sysroot first:

```sh
cd ~/Sources/usr-amix/amix-userland/components/library/ncurses
make build
make -C build install DESTDIR=$HOME/opt/m68k-amix-sysroot

cd ../../sysutils/amtop
make build
```

The result is `build/amtop`. Run `make clean` to remove what a build made.

Things worth knowing:

- Downloads are checked against the checksum in the `Makefile`.
- Some components need another one first. The `Makefile` and `README.md` say so:
  `multimedia/ffmpeg` needs `library/sdl2`, for example.
- `SHM_LIBS=<dir>` is needed by programs using MIT-SHM. It names a directory with
  a static `libXext.a` that has XShm, and `libshmsys.a`.
- A few components only prepare a tree to build on the Amiga, with `make prep`.
- `games/` has no game data. Use your own copy.

## 5. Copy it to the Amiga

Network transfer uses two small tools in `tools/net`: `netsend.py` on the PC and
`netget` on the Amiga.

**Once only: get `netget` onto the Amiga.** Build it:

```sh
cd ~/Sources/usr-amix/amix-userland/tools/net
~/opt/m68k-amix/bin/m68k-elf-gcc -O2 -m68060 -o netget netget.c -lsocket -lnsl
```

Copy that file to `/usr/local/bin/netget` on the Amiga any way you can, such as
FTP or a disk, then run `chmod +x /usr/local/bin/netget`.

**From then on,** pack the program in the layout it has on the Amiga:

```sh
cd ~/Sources/usr-amix/amix-userland/components/sysutils/amtop
rm -rf pkg
mkdir -p pkg/usr/local/bin
cp build/amtop pkg/usr/local/bin/
tar cf amtop.tar -C pkg usr
python3 ../../../tools/net/netsend.py amtop.tar 5555
```

`netsend.py` waits for one connection. On the Amiga, as root, using your PC's
address:

```sh
cd /tmp
netget 192.168.0.10 5555 amtop.tar
cd /
tar xf /tmp/amtop.tar
rm /tmp/amtop.tar
```

Then run `amtop`. Remove `pkg` and `amtop.tar` on the PC when you're done.

Watch the disk space on the Amiga with `df -k /`, and clean up in `/tmp` after
each transfer.

## 6. Kernel and drivers

These need GCC 2.7.2.3 and amix-gate. See
[doc/building.md there](https://github.com/isoriano1968/amix-gate/blob/main/doc/building.md).
The same sysroot works for both compilers.

## Adding a component

Start from `templates/Makefile`. To bring in an existing repository, see
[moving-a-repo.md](moving-a-repo.md).

## Common problems

| Problem | Cause |
| ------- | ----- |
| `no such file` for `m68k-elf-gcc` | GCC 14 isn't in `~/opt/m68k-amix`. Set `AMIX_TOOLS=<dir>/bin` on the `make` line. |
| Program dies at address 0 when using X | `tools/fix-x-libraries.sh` hasn't been run. |
| Program dies with an illegal instruction on a 68060 | Check it with `developer/gcc-14/tools/check-68060-safe.py`. |
| `netget` hangs | `netsend.py` must be started first, and your PC's firewall must allow the port. |
| Text file busy when copying | The program is running on the Amiga. Stop it first. |
