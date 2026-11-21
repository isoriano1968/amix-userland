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
- A CDE "Hello, world" sample project, and a console (curses) one.
- Run on Target: builds the program, sends it to an AMIX machine (a real
  Amiga or FS-UAE) and runs it there, in a VS Code terminal.

## Requirements

Install and validate an AMIX cross toolchain first.

On OpenIndiana, from the amix publisher:

```sh
sudo pkg install metapackages/amix-cross-dev   # or pfexec with a pkg profile
```

That brings `developer/amix/sysroot` (`/opt/m68k-amix-sysroot`),
`developer/amix/gcc-27` (`/opt/amix-cross/bin`) and `developer/amix/gcc-14`
(`/opt/m68k-amix/bin`). X11R6.3 and Motif come as
`developer/amix/x11r6.3` and `developer/amix/motif`, which the CDE Hello World
sample links against.

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
make install      # make build only packages build/vscode-amix-0.5.1.vsix
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
- `AMIX: Run on Target` (also the ▷ button of a C file or Makefile)
- `AMIX: Create Agent Key` / `AMIX: Set Agent Key`
- `AMIX: Show Agent Configuration (/etc/vscode-agent.conf)`
- `AMIX: Start Agent Listener` / `AMIX: Stop Agent Listener`
- `AMIX: Select Target`
- `AMIX: Set Up an AMIX Target (vscode-agent)`: how to install the agent, with what is missing now

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
-lXm -lXmu -lXt -lSM -lICE -lXext -lX11 -lsocket -lnsl -lgen -lm
```

with the runpath `/usr/motif/lib:/usr/x11r6/lib`, and nothing else: the
program needs no libamixc99 (libXm brings its own). On the A3000, copy `hello`
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
`library/ncurses`), shared from the gcc 14 sysroot for both compilers, with
the flags `pkg-config --cflags --libs ncurses` reads from the sysroot's
`.pc` files (`ncurses.pc` requires `amixc99.pc`, the shared
`/usr/lib/libamixc99.so.1`):

```text
-I<sysroot>/usr/local/include  -L<sysroot>/usr/local/lib -lncurses
-L<sysroot>/usr/lib -lamixc99 -lgen -lm
```

with the runpath `/usr/local/lib`. The A3000 needs the `library/ncurses`
package (`/usr/local/lib/libncurses.so.6` and about 1900 terminfo files under
`/usr/share/terminfo` on root; on a small root use
`pkg --min-free 20 install library/ncurses`, since pkg keeps 200 MB free by
default). Then, in any terminal:

```sh
./hello
```

On the console `TERM` is already `amiga`; elsewhere any entry the package
installs (`vt100`, `xterm`, `dtterm`).

## Run on Target

**AMIX: Run on Target** runs the program you build on an AMIX machine:
1. It runs make in the project.
2. It sends the program to the machine.
3. It starts the program there, in a VS Code terminal.

Console programs, curses ones included, get a real terminal on AMIX: keys,
colours and resizing work. X and CDE programs open on the Amiga's own
screen (`DISPLAY=:0`). When a program dumps core, the core file comes back
into the project as `core.<program>`.

The AMIX side is **vscode-agent**
([developer/vscode-agent](../vscode-agent)). It starts at boot and dials
out to VS Code, so it also works in FS-UAE, whose NAT lets nothing dial
in, and on a stock AMIX without ssh. Both ends prove that they know a
shared key before anything runs. The programs run as a normal user, never
as root, each in a session folder under `/tmp` that is removed when VS
Code goes away.

Setting it up (**AMIX: Set Up an AMIX Target** shows the same, with this
computer's state; Run on Target and the status bar lead there while no
agent has signed in, and **AMIX: Check Development Setup** lists it):
1. **With the AMIX Installer:** in its Development step, switch on **VS
   Code agent**, or click **Only the VS Code agent** for a console system
   that is only a target. The installer writes `/etc/vscode-agent.conf`
   with a new key and leaves the key in `vscode-agent.key` beside its
   report. On the same computer the extension finds that file: with no
   key of its own it takes it, else the first refused agent brings **Use
   the Installer's Key** (or run **AMIX: Set Agent Key**: the installer's
   key, **Choose a key file...** for a `vscode-agent.key` or a copy of
   `/etc/vscode-agent.conf`, or **Paste a key**, where a path to such a
   file works too).
   For a quick test into an AMIX that is already running, **Just the
   Agent** (`amix-installer --running fs-uae --agent-only --net`) installs
   only the agent, with the same key file.
2. **By hand:**
   1. Install the agent on AMIX: `pkg install developer/vscode-agent`.
   2. In VS Code, run **AMIX: Create Agent Key**. It opens the
      configuration to use, with the key and this computer's addresses
      (FS-UAE sees this computer as `10.0.2.2`).
   3. On AMIX, as root, save it as `/etc/vscode-agent.conf`, set the
      `user` line to your login, then run:
      `chmod 600 /etc/vscode-agent.conf; /etc/init.d/vscode-agent start`.

**Restricted Mode:** a folder VS Code does not trust yet (a new sample
project, say) opens with the AMIX commands off, since they run the
folder's make; each of them then offers **Trust This Folder**.

**Resizing:** the program starts once the terminal's size has settled,
and a later change reaches the program as SIGWINCH (AMIX's pty sends it). Stock SVR4 curses
does not follow a resize by itself; the console sample shows how
(a new screen on SIGWINCH).

### launch.json

The launch type `amix` puts Run on Target into VS Code's **Run and Debug**
view. **Ctrl+F5** (Run Without Debugging) and **F5** then start the
program on the target. New sample projects come with the entry, and
**AMIX: Configure Workspace** adds a `launch.json` when there is none.
With **Add Configuration…** the snippet is called "AMIX: Run on AMIX
target".

```json
{
  "version": "0.2.0",
  "configurations": [{
    "type": "amix",
    "request": "launch",
    "name": "Run on AMIX target",
    "cwd": "${workspaceFolder}",
    "program": "hello",
    "args": [],
    "env": { "LANG": "C" },
    "display": ":0",
    "files": [],
    "buildFirst": true
  }]
}
```

Each attribute overrides the `amix.run.*` setting of the same name. The
program runs in its own terminal, and **Stop** ends it. F5 does not stop
at breakpoints yet: that needs gdb on AMIX. Until then it runs the
program like Ctrl+F5 and says so in the Debug Console.

The status bar shows the connected target. With several targets, **AMIX:
Select Target** chooses one.

| Setting | Default | Meaning |
|---|---|---|
| `amix.run.program` | found | the program, relative to the Makefile's folder; empty: the AMIX executable there |
| `amix.run.args` | `[]` | its arguments |
| `amix.run.display` | `:0` | `DISPLAY` for X and CDE programs; empty: none |
| `amix.run.env` | `{}` | more environment variables |
| `amix.run.files` | `[]` | more files to send (data, resources) |
| `amix.run.buildFirst` | `true` | run make first; a failed build sends nothing |
| `amix.run.fetchCore` | `true` | fetch a core file |
| `amix.agent.port` | `7777` | the port the agent dials |
| `amix.agent.listenAddress` | `0.0.0.0` | where the listener binds |
| `amix.agent.autoStart` | `true` | listen as soon as a key exists |

Use the agent on your own network or in an emulator. After the sign-in
the connection is not encrypted.

## Notes

C/C++ semantic browsing is provided by the installed VS Code C/C++ extension
when available. This extension writes `C_Cpp.default.*` settings so that AMIX
headers are visible to IntelliSense.
