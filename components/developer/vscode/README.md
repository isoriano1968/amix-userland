<p align="center"><img src="images/amix-logo.png" alt="AMIX" width="128"></p>

# AMIX Development Tools for VS Code

VS Code helpers for AMIX C development with two cross toolchains: gcc 2.7.2.3
(`m68k-cbm-sysv4-gcc`) or gcc 14 (`gcc14-cross-amix`, `m68k-elf-gcc`).

## Features

- A choice of toolchain, gcc 2.7.2.3 or gcc 14, in the status bar.
- Commands for building the current C file as an AMIX executable or object.
- Workspace task generation for userland, kernel/driver, and Makefile builds.
- AMIX sysroot include paths for C/C++ language tooling.
- GCC 2.7 style problem matcher.
- Basic m68k assembly highlighting for `.m68k`, `.68k`, and `.amixs`.
- Small snippets for AMIX userland, driver and Motif/CDE code.
- A CDE "Hello, world" sample project.

## Requirements

Install and validate an AMIX cross toolchain first.

On OpenIndiana, from the amix publisher:

```sh
sudo pkg install metapackages/amix-cross-dev   # or pfexec with a pkg profile
```

That brings `developer/amix/sysroot` (`/opt/m68k-amix-sysroot`),
`developer/amix/gcc-27` (`/opt/amix-cross/bin`) and `developer/amix/gcc-14`
(`/opt/m68k-amix/bin`). X11R6.3, Motif and CDE are not packaged yet, so X and
CDE programs (the CDE Hello World sample) do not link from the packages alone.

On Linux, built by hand under `~/opt`. For gcc 2.7.2.3:

```sh
make all AMIX_ROOT=/path/to/usr-amix
. build/env.sh
m68k-cbm-sysv4-gcc hello.c -o hello
```

For gcc 14, install gcc14-cross-amix (`m68k-elf-gcc` in `~/opt/m68k-amix/bin`)
and its sysroot `~/opt/m68k-amix-sysroot` with the shared X11R6.3, Motif and
CDE libraries.

The AMIX sysroot is supplied by the user from a local AMIX installation. This
extension does not include AMIX headers, libraries, or startup objects.

## Toolchains

| `amix.toolchain` | Compiler | Use |
| --- | --- | --- |
| `gcc272` (default) | gcc 2.7.2.3, `m68k-cbm-sysv4-gcc` | kernel, drivers and userland; native-compatible |
| `gcc14` | gcc 14, `m68k-elf-gcc` | userland only (never the kernel), usually `-m68060` |

Switch with the status bar item (`AMIX gcc 2.7.2.3` / `AMIX gcc 14`) or
`AMIX: Select Toolchain`. The selected toolchain is used by the build commands,
`AMIX: Run Make`, the tasks written by `AMIX: Configure Workspace`, and the
C/C++ IntelliSense settings. Kernel/driver objects are always compiled with
gcc 2.7.2.3.

Each toolchain has its own path and tool prefix:

| Setting | gcc 2.7.2.3 | gcc 14 |
| --- | --- | --- |
| tools directory | `amix.toolchainPath` | `amix.gcc14.toolchainPath` |
| tool prefix | `amix.target` + `-` (`m68k-cbm-sysv4-`) | `amix.gcc14.toolPrefix` (`m68k-elf-`) |
| sysroot | `amix.sysroot`, `amix.crtDir` | `amix.gcc14.sysroot` |
| CPU | `amix.cpu` (`68020`) | `amix.gcc14.cpu` (`68060`) |
| C flags | `amix.extraCFlags` | `amix.gcc14.extraCFlags` |

Left empty (the default), the paths are found: the OpenIndiana packages
first, then the hand-built layout under `~/opt`:

| | packaged | `~/opt` |
| --- | --- | --- |
| gcc 2.7.2.3 tools | `/opt/amix-cross/bin` | `~/opt/amix-cross/bin` |
| gcc 2.7.2.3 sysroot | `/opt/m68k-amix-sysroot` | `~/opt/amix-cross/m68k-cbm-sysv4/sysroot` |
| gcc 14 tools | `/opt/m68k-amix/bin` | `~/opt/m68k-amix/bin` |
| gcc 14 sysroot | `/opt/m68k-amix-sysroot` | `~/opt/m68k-amix-sysroot` |

A toolchain's sysroot follows its tools: the packaged gcc uses the packaged
sysroot. `amix.crtDir` empty is `<sysroot>/usr/ccs/lib`. `amix.toolchainPath`
keeps its meaning from earlier versions: the gcc 2.7.2.3 tools directory.

The status bar shows the toolchain and the sysroot version from its marker
`etc/amix-sysroot.release` (`AMIX gcc 14 · sysroot 2.1.2`, from
`package=developer/amix/sysroot@2.1.2`; else `amix-release`, else `kind`); the tooltip has
the paths. `AMIX: Check Development Setup` lists both toolchains, the
sysroots, X11R6.3/Motif/CDE and the driver kit, with the `pkg install` line
for what is missing.

With gcc 14, `AMIX: Build Current C File as Executable` runs
`amix-fix-needed` on the result (`amix.gcc14.fixNeeded`): AMIX's `ld.so.1`
reads NEEDED names from the start of `.dynstr`, where GNU ld does not put them.

## Install

From this component directory (`components/developer/vscode` in
amix-userland):

```sh
make install      # make build only packages build/vscode-amix-0.5.0.vsix
```

`make build` runs `npx @vscode/vsce package` with the component's GitHub
location, so the README's logo and links resolve in the packaged extension.

Reload VS Code after installing:

```text
Developer: Reload Window
```

For extension development, open this directory in VS Code and press `F5`.
That starts an Extension Development Host with the extension loaded.

## Setup

Open an AMIX source workspace and run:

```text
AMIX: Configure Workspace
```

This writes:

```text
.vscode/settings.json
.vscode/tasks.json
```

The toolchain paths are not written there: they are found as described under
Toolchains. Set the `amix.*` path settings only if your toolchains are
elsewhere. After switching
toolchain, the extension offers to rewrite `.vscode/settings.json` and
`tasks.json` (other settings in `settings.json` are kept).

## Commands

- `AMIX: Configure Workspace`
- `AMIX: Select Toolchain (gcc 2.7.2.3 / gcc 14)`
- `AMIX: Check Development Setup`
- `AMIX: Build Current C File as Executable`
- `AMIX: Compile Current C File to Object`
- `AMIX: Compile Current C File as Kernel/Driver Object`
- `AMIX: Run Make in AMIX Project (selected toolchain)`
- `AMIX: Run Make Clean in AMIX Project (selected toolchain)`
- `AMIX: Run Make in AMIX Project with gcc 2.7.2.3` / `with gcc 14`
- `AMIX: Run Make Clean in AMIX Project with gcc 2.7.2.3` / `with gcc 14`
- `AMIX: New CDE Hello World Project`
- `AMIX: New Console (curses) Hello World Project`

## Makefile Builds

For larger projects such as drivers, open any file inside the project tree and
run:

```text
AMIX: Run Make in AMIX Project
```

The extension searches from the active file directory upward until it finds a
`Makefile`. It then runs `make` in that directory with the cross-toolchain in
`PATH` and the variables listed below set.

The AMIX Makefiles are GNU make. On illumos and Solaris, where `make` is Sun
make ("badly formed macro assignment"), the extension runs `gmake` instead;
`amix.makeCommand` overrides the choice on any system.

The generated `tasks.json` has build, object, nm, make and make clean tasks for
both toolchains, labelled `(gcc 2.7.2.3)` and `(gcc 14)`; the selected
toolchain's executable build is the default build task. The make tasks run in
the active file directory. For nested source trees, prefer the command palette
command above because it searches parent directories.

If the Makefile recipe hardcodes native tools such as `cc`, `ld`, or `ar`, the
extension shows a warning. Use make variables for cross builds:

```make
CC ?= cc
LD ?= ld
AR ?= ar

exp: $(OBJ)
	$(LD) -r -o exp $(OBJ)
```

The variables, with the selected toolchain's prefix (`m68k-cbm-sysv4-` for
gcc 2.7.2.3, `m68k-elf-` for gcc 14) and its directory first in `PATH`:

```text
CC=<prefix>gcc
AS=<prefix>as
LD=<prefix>ld
AR=<prefix>ar
NM=<prefix>nm
RANLIB=<prefix>ranlib
OBJDUMP=<prefix>objdump
STRIP=<prefix>strip
AMIX_TOOLCHAIN=gcc272 | gcc14
AMIX_SYSROOT=<the toolchain's sysroot>
AMIX_CRT_DIR=<the toolchain's crt directory>
AMIX_MOTIF_SYSROOT=<amix.gcc14.sysroot, for Motif/X11R6.3>
```

A Makefile can choose flags with `AMIX_TOOLCHAIN`, as the CDE sample does.

Additional make arguments can be configured with:

```json
{
  "amix.makeArgs": ["CPUFLAGS=-m68020"]
}
```

## CDE Hello World

`AMIX: New CDE Hello World Project` copies a small Motif/CDE program into the
workspace (a folder `cde-hello` by default; with no workspace open it asks for
a parent folder and opens the new one): `hello.c`, a `Makefile` for both
toolchains, `amix-fix-needed` and a README. The snippet `amix-cde-hello` gives
the same program in any C file.

```c
top = XtVaAppInitialize(&app, "Hello", NULL, 0, &argc, argv, NULL, NULL);
form = XtVaCreateManagedWidget("form", xmFormWidgetClass, top, NULL);
/* an XmLabel "Hello, world" and an XmPushButton "Quit" in the form */
XtRealizeWidget(top);
XtAppMainLoop(app);
```

Build it with `AMIX: Run Make in AMIX Project` (the toolchain in the status
bar), or in a terminal:

```sh
make                    # gcc 14, -m68060
make TOOLCHAIN=gcc272   # gcc 2.7.2.3, -m68020
```

Motif and X11R6.3 come from the gcc 14 sysroot for both compilers; the link
line is ztopcde's:

```text
-lXm -lXmu -lXtInherit -lXt -lSM -lICE -lXext -lX11 -lsocket -lnsl -lamixc99 -lgen -lm
```

with the runpath `/usr/motif/lib:/usr/x11r6/lib`. On the A3000, copy `hello`
over and start it from a CDE dtterm (`./hello`), or from an ssh login on the
console's X server:

```sh
DISPLAY=:0 ./hello
```

## Console Hello World

For a program without X, `AMIX: New Console (curses) Hello World Project`
copies a curses terminal program (folder `console-hello`): a framed window
with the system name, a ticking clock and a small menu (arrow keys, Enter,
`q`). It runs on the console, in a dtterm or over ssh. The snippet
`amix-curses-hello` gives the same program in any C file.

It builds like the CDE sample (`make`, `make TOOLCHAIN=gcc272`, or
`AMIX: Run Make in AMIX Project`). curses is ncurses 6.6 (amix-userland
`library/ncurses`), shared from the gcc 14 sysroot's `usr/local` for both
compilers:

```text
-I<sysroot>/usr/local/include  -L<sysroot>/usr/local/lib -lncurses -lamixc99
```

with the runpath `/usr/local/lib`. The A3000 needs the `library/ncurses`
package (`/usr/local/lib/libncurses.so.6` and its terminfo); then, in any
terminal:

```sh
TERM=vt100 ./hello
```

## Notes

C/C++ semantic browsing is provided by the installed VS Code C/C++ extension
when available. This extension writes `C_Cpp.default.*` settings so that AMIX
headers are visible to IntelliSense.
