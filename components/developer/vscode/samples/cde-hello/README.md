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

The Makefile needs GNU make: on OpenIndiana or Solaris type `gmake`. The
toolchain's `bin` directory must be on `PATH`, unless it is the OpenIndiana package's (`/opt/m68k-amix/bin`, `/opt/amix-cross/bin`), which the Makefile finds itself. From VS Code, run
**AMIX: Run Make in AMIX Project**: it sets `PATH`, `AMIX_TOOLCHAIN` and the
sysroots for the toolchain shown in the status bar.

Motif and X11R6.3 come from the gcc 14 sysroot for both compilers:
`/opt/m68k-amix-sysroot` when the OpenIndiana package is installed, else
`~/opt/m68k-amix-sysroot` (`MOTIF_SYSROOT=` to change it): on OpenIndiana the
packages developer/amix/x11r6.3 and developer/amix/motif. Only shared
libraries are linked; the link line is the one of ztopcde:

```text
-lXm -lXmu -lXt -lSM -lICE -lXext -lX11 -lsocket -lnsl -lgen -lm
```

with the runpath `/usr/motif/lib:/usr/x11r6/lib`. The program needs no
libamixc99: libXm brings what it needs itself (`libamixc99.so.1` for its
`snprintf`, from library/amixc99, which desktop/motif requires). After linking,
`amix-fix-needed` moves the NEEDED names to the front of `.dynstr`, where
AMIX's `ld.so.1` looks for them.

Both compilers link through their driver. The gcc 2.7.2.3 wrapper
(`m68k-cbm-sysv4-gcc`) needs gcc-cross-amix 83765c5 or later: older wrappers
linked only `libc.so.1`, not AMIX's libc archive `usr/ccs/lib/libc.so`
(`select`, `syslog`, `gettimeofday`, ... for libsocket and libXt), and
dropped `-Wl,` options.

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
