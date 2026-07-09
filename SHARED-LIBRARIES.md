# AMIX shared-library ABI

AMIX uses the SVR4 runtime loader with a split shared-code/shared-data model.
The details below were validated on AMIX with GCC 2.7.2.3 and the native link
editor. They are required for dynamically linked X11R6 clients.

## Runtime images and stub archives

- `libfoo.so.N` is an ELF runtime image.
- `libfoo.so` is a symbolic link to that ELF image for `-lfoo`.
- `libXt.sa` and `libXmu.sa` are ordinary archives containing `sharedlib.o`.
- Xaw does not use a companion `.sa` archive on AMIX.
- A `.sa` archive must never replace or be named as `libfoo.so`.

The stub object supplies executable-resident entry points and data required by
the AMIX shared-data ABI. `amix.rules` makes the archive an explicit part of
the client link because GCC does not add it automatically.

## Required link order

The AMIX linker resolves archives from left to right. Put a companion `.sa`
immediately after its corresponding shared library, and put providers after
their users. A representative Xt/Xaw client link is:

```text
-lXaw -lXmu /path/libXmu.sa -lXt /path/libXt.sa \
    -lSM -lICE -lXext -lX11 -lsocket -lnsl
```

Moving either `.sa` archive away from its `-l` option can leave wrapper or
underscore-prefixed implementation symbols unresolved. Omitting `libXt.sa`
causes failures such as an unresolved `XtOpenApplication` or
`XtToolkitInitialize`.

Do not use the stock SVR4 `XawClientLibs` expansion on AMIX. It can emit
`-lXaw -lXmu -lXt ...` without `libXmu.sa` and `libXt.sa`, which is enough for
some clients to link accidentally but fails for Athena clients that call Xt
entry points directly, including `xlogo` and `xmag`.

## Building ELF images

Compile shared objects with `gcc -fpic`, but link the runtime image directly
with the native linker:

```text
ld -G -h libXt.so.6.0 -o libXt.so.6.0 object-list
```

Both `-G` and the `-h` SONAME are required. Do not use `gcc -G`: GCC 2.7.2.3
adds startup objects intended for an executable, producing runtime errors
about a missing `main`. Do not add `-Bsymbolic`; it was tested and did not
solve the loader failure.

Never reuse a shared image produced while experimenting with different link
flags. In testing, a stale `libICE.so.6.3` caused every Xt client to crash
before `main`; rebuilding it with `ld -G -h` fixed the failure.

## Validation sequence

1. Validate every `libfoo.so.N` with `file` and `dump -Lv`.
2. Confirm `libXt.sa` and `libXmu.sa` each contain only `sharedlib.o`.
3. Relink and test `xinit`, `xsetroot`, `xclock`, `xterm`, and `twm`.
4. Start `Xzz9000` only after the client-side ABI tests pass.

`amix-shared-rebuild.sh` performs these structural checks and regenerates the
affected client link lines.
