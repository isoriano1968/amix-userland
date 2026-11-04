# zstd

Zstandard 1.5.7 for AMIX: `libzstd.so.1` in `/usr/local/lib`, with `zstd.h`,
`zdict.h`, `zstd_errors.h` and upstream's `libzstd.pc`. The library only.

The example of a Meson component: `make-rules/meson.mk` writes the cross file
and runs upstream's `build/meson`. Single-threaded (AMIX has no kernel
threads), without the legacy pre-1.0 formats (their decoders mix `U32` types
on AMIX, where `uint32_t` is `unsigned long`).
