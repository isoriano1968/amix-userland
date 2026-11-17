# Console Hello World for AMIX

A native AMIX terminal program with curses: a framed window with the system
name, a clock that ticks every second and a small menu (arrow keys or `j`/`k`,
Enter, `q` to quit). It runs on the console, in a dtterm or over ssh, with no
X at all. It uses only classic curses calls (`initscr`, `newwin`, `box`,
`wattron`, `halfdelay`, `wgetch`, `endwin`), as in SVR4 curses and ncurses.

## Build

On the build host, with either AMIX cross toolchain:

```sh
make                    # gcc 14 (m68k-elf-gcc, -m68060)
make TOOLCHAIN=gcc272   # gcc 2.7.2.3 (m68k-cbm-sysv4-gcc, -m68020)
```

The Makefile needs GNU make: on OpenIndiana or Solaris type `gmake`. The
toolchain's `bin` directory must be on `PATH`. From VS Code, run
**AMIX: Run Make in AMIX Project**: it sets `PATH`, `AMIX_TOOLCHAIN` and the
sysroots for the toolchain shown in the status bar.

curses is ncurses 6.6 (amix-userland `library/ncurses`), linked shared from
the gcc 14 sysroot's `usr/local` for both compilers (`LIB_SYSROOT=` to change
it):

```text
-I<sysroot>/usr/local/include  -L<sysroot>/usr/local/lib -lncurses -lamixc99
```

with the runpath `/usr/local/lib`. After linking, `amix-fix-needed` moves the
NEEDED names to the front of `.dynstr`, where AMIX's `ld.so.1` looks for them.

## Run on the A3000

The A3000 needs the `library/ncurses` package: `/usr/local/lib/libncurses.so.6`
and its terminfo, about 1900 small files under `/usr/share/terminfo` on the
root file system. pkg keeps 200 MB free on root by default, so on a small root
it refuses the install; `pkg --min-free 20 install library/ncurses` lowers
that reserve to 20 MB for this install.

Copy `hello` to the A3000 (scp), then in any terminal:

```sh
./hello
```

On the console `TERM` is already `amiga` (set by `/etc/profile`); elsewhere
`TERM` must name an entry the package installs (`vt100`, `xterm`, `dtterm`),
and a dtterm sets `TERM=dtterm` itself. `q` or the Quit item ends it.
