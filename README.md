# AMIX Development Tools for VS Code

VS Code helpers for AMIX C development with the `gcc-cross-amix` toolchain.

## Features

- Commands for building the current C file as an AMIX executable or object.
- Workspace task generation for userland and kernel/driver builds.
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

## Notes

C/C++ semantic browsing is provided by the installed VS Code C/C++ extension
when available. This extension writes `C_Cpp.default.*` settings so that AMIX
headers are visible to IntelliSense.
