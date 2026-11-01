# Motif for AMIX

Motif 2.3.8: `libXm`, `libMrm`, their headers and `.pc` files, and `mwm`, in `/usr/motif`. Needs `x11/x11r6.3` (see `motif.manifest`).

`make build` makes the static libraries first, then the shared ones in `SHARED_STAGE`, then `work/motif-amix.tar` with `mwm`. configure cannot run test programs for AMIX, so `files/motif-amix.cache` holds its answers. The tools Motif runs while it builds (makestrs, wml) are compiled for the host.
