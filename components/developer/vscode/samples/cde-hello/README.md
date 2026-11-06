# CDE Hello World for AMIX

The smallest Motif/CDE program: a window with "Hello, world" and a Quit
button (`XtVaAppInitialize`, an `XmForm` with an `XmLabel` and an
`XmPushButton`).

## Build

On the Linux PC, with either AMIX cross toolchain:

```sh
make                    # gcc 14 (m68k-elf-gcc, -m68060)
make TOOLCHAIN=gcc272   # gcc 2.7.2.3 (m68k-cbm-sysv4-gcc, -m68020)
```

The toolchain's `bin` directory must be on `PATH`. From VS Code, run
**AMIX: Run Make in AMIX Project**: it sets `PATH`, `AMIX_TOOLCHAIN` and the
sysroots for the toolchain shown in the status bar.

Motif and X11R6.3 come from the gcc 14 sysroot (`~/opt/m68k-amix-sysroot`,
`MOTIF_SYSROOT=` to change it) for both compilers. The link line is the one
of ztopcde:

```text
-lXm -lXmu -lXtInherit -lXt -lSM -lICE -lXext -lX11 -lsocket -lnsl -lamixc99 -lgen -lm
```

with the runpath `/usr/motif/lib:/usr/x11r6/lib`. After linking,
`amix-fix-needed` moves the NEEDED names to the front of `.dynstr`, where
AMIX's `ld.so.1` looks for them.

With gcc 2.7.2.3 the Makefile links with `m68k-cbm-sysv4-ld` itself: the gcc
wrapper links only `libc.so.1`, while AMIX's `-lc` is the archive
`usr/ccs/lib/libc.so` (libc.so.1 plus static members such as `select`,
`syslog` and `gettimeofday`, which libsocket, libXt and libamixc99 need).

## Run on the A3000

Copy `hello` to the A3000 (scp), then in a CDE dtterm:

```sh
./hello
```

or from a login without X (ssh), on the X server of the console:

```sh
DISPLAY=:0 ./hello
```

Quit closes the window.
