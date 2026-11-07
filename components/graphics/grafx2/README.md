# Grafx2 for AMIX

[Grafx2](https://grafx2.gitlab.io/grafX2/) 2.9, the 256-colour pixel art paint program made as a
"tribute to Deluxe Paint": brushes and brush effects, stencils, gradients, colour cycling,
shading, layers and animation, and the classic Amiga, Atari ST, C64 and CPC picture formats
(IFF/ILBM, GIF, PNG, PCX, ...). It uses its own X11 front end, so it needs only Xlib, libpng and zlib.

- `grafx2 [picture]`, from `/usr/local/bin`. Its skins, fonts and defaults are in
  `/usr/local/share/grafx2`, and each user's settings in `~/.grafx2`.
- CDE gets the Grafx2 action, icons, an App Manager entry and a control in the AMIX
  subpanel (`desktop/cde-apps/panel/amix.fp`).
- The X server must have a TrueColor visual. AMIX's Xzz9000 runs at 16 bits.

The component is the upstream release plus redcode's 6502 emulator v0.1 (for loading
C64 programs, fetched by upstream's own build too), with two patches:

- `0001-amix-port.patch`: no free-memory call (default size), the SVR4 mount table,
  settings in `~/.grafx2`, and the installed `bin` directory when `argv[0]` has none.
- `0002-x11-any-truecolor.patch`: the X11 front end for any TrueColor visual (pixel
  values from the visual's masks, the image in the host's byte order), instead of
  32-bit little-endian only.

`files/` has the build rules (`Makefile.amix`, no autoconf) and, under `amix/`, what
AMIX's libc lacks: `endian.h`, `strings.h`, an ASCII `wctype.h` and `round()`.

Left out for now: Lua scripts, TrueType text (SDL_ttf), TIFF and the recoil formats.

Build: `make build stage`. Install on AMIX as root:
`cd / && /usr/gnu/bin/tar --no-overwrite-dir -xf grafx2-amix.tar`.

Grafx2 is GPLv2. The 6502 emulator is GPLv3 with a relicence to Grafx2 under GPLv2;
see `grafx2.license`.
