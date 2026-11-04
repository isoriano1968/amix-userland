# bzip2

bzip2 1.0.8 for AMIX: the `bzip2`, `bunzip2`, `bzcat` and `bzip2recover`
programs in `/usr/local/bin`, and `libbz2.so.1.0` with `bzlib.h` and
`bzip2.pc` (upstream ships no `.pc`; ours is `files/bzip2.pc.in`).

The example of a component without a build system of its own: the recipe
compiles upstream's files itself, with `make-rules/unpack.mk` and `shlib.mk`.
`patches/0001` declares `fchmod`, which AMIX's `sys/stat.h` lacks.

Replaces the static 1.0.2 package of 2005 (`JSbzip2`): remove that with
`pkgrm JSbzip2` first, since it registers the same paths.
