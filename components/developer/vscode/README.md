# AMIX Development Tools for VS Code

VS Code helpers for AMIX C development with the `gcc-cross-amix` toolchain.

## Features

- Commands for building the current C file as an AMIX executable or object.
- Workspace task generation for userland, kernel/driver, and Makefile builds.
- AMIX sysroot include paths for C/C++ language tooling.
- GCC 2.7 style problem matcher.
- Basic m68k assembly highlighting for `.m68k`, `.68k`, and `.amixs`.
- Small snippets for AMIX userland and driver code.

## Requirements

Install and validate the AMIX cross toolchain first:

```sh
make all AMIX_ROOT=/path/to/usr-amix
. build/env.sh
m68k-cbm-sysv4-gcc hello.c -o hello
```

The AMIX sysroot is supplied by the user from a local AMIX installation. This
extension does not include AMIX headers, libraries, or startup objects.

## Install

From the extension source directory:

```sh
npm install --save-dev @vscode/vsce
npx vsce package --allow-missing-repository --skip-license
code --install-extension vscode-amix-0.1.0.vsix
```

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

The default settings assume:

```text
~/opt/amix-cross/bin
~/opt/amix-cross/m68k-cbm-sysv4/sysroot
```

Update the `amix.*` settings if your toolchain is elsewhere.

## Commands

- `AMIX: Configure Workspace`
- `AMIX: Build Current C File as Executable`
- `AMIX: Compile Current C File to Object`
- `AMIX: Compile Current C File as Kernel/Driver Object`
- `AMIX: Run Make in AMIX Project`
- `AMIX: Run Make Clean in AMIX Project`

## Makefile Builds

For larger projects such as drivers, open any file inside the project tree and
run:

```text
AMIX: Run Make in AMIX Project
```

The extension searches from the active file directory upward until it finds a
`Makefile`. It then runs `make` in that directory with the cross-toolchain in
`PATH` and these variables set:

The generated `tasks.json` also includes make tasks, but VS Code tasks run in
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

```text
CC=m68k-cbm-sysv4-gcc
AS=m68k-cbm-sysv4-as
LD=m68k-cbm-sysv4-ld
AR=m68k-cbm-sysv4-ar
NM=m68k-cbm-sysv4-nm
RANLIB=m68k-cbm-sysv4-ranlib
AMIX_SYSROOT=<configured sysroot>
AMIX_CRT_DIR=<configured crt directory>
```

Additional make arguments can be configured with:

```json
{
  "amix.makeArgs": ["CPUFLAGS=-m68020"]
}
```

## Notes

C/C++ semantic browsing is provided by the installed VS Code C/C++ extension
when available. This extension writes `C_Cpp.default.*` settings so that AMIX
headers are visible to IntelliSense.

## Planned

Both cross toolchains from one extension: gcc 2.7.2.3 (`m68k-cbm-sysv4-gcc`)
for the kernel and drivers, gcc 14 (`m68k-elf-gcc`) for userland, chosen per
build task, each with its own problem matcher. Today only 2.7.2.3 is set up.
