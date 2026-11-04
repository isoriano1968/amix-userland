# gzip

GNU gzip 1.14 for AMIX, in `/usr/local/bin`: `gzip`, and its scripts
(`gunzip`, `zcat`, `zgrep`, `zdiff`, `zless`, ...) with `uncompress` as a link
to `gunzip`.

In `/usr/local` rather than `/usr/gnu`: AMIX has no gzip of its own to
shadow, and `tar z` and scripts look for `gzip` on the `PATH`
(`GNU_PREFIX = /usr/local` before `../gnu.mk`). The scripts use `$(...)`,
which AMIX's `/bin/sh` lacks: their first line is `/usr/gnu/bin/bash`
(`shell/bash`).

Replaces the old `FSFgzip` package: `pkgrm FSFgzip` first.
