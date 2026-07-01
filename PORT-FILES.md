# Port overlay

`overlay/xc` is copied over a pristine X11R6.3 `xc` source tree.

## Build configuration

- `config/cf/amix.cf`: AMIX platform, GCC 2.7.2.3, shared libraries, no MIT-SHM
- `config/cf/amix.rules`: old cpp and AMIX archive compatibility rules
- `config/cf/Imake.cf`, `config/cf/site.def`: AMIX platform selection
- `config/imake/Makefile.ini`, `config/imake/imakemdep.h`: bootstrap compiler/cpp
- `config/util/amix-shared-rebuild.sh`: clean PIC library and client rebuild
- `programs/Imakefile`, `programs/rstart/Imakefile`: old cpp/make compatibility

## X server

- `programs/Xserver/Imakefile`: `Xzz9000` target and AMIX DDX dependency graph
- `programs/Xserver/include/servermd.h`: m68k AMIX image/bitmap padding and order
- `programs/Xserver/hw/amix`: AMIX keyboard, mouse, console, and server glue
- `programs/Xserver/hw/amix/rtg`: generic AMIX RTG screen layer
- `programs/Xserver/hw/amix/rtg/zz9000`: ZZ9000 16-bit framebuffer DDX

Generated Makefiles, object files, libraries, executables, fonts, and the
upstream X11R6.3 distribution are intentionally not tracked.

