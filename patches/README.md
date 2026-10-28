# AMIX patches for GNU Pth 2.0.7

These LGPL 2.1-or-later port changes apply to the pinned GNU Pth release.
The build recipe normally applies the equivalent idempotent Python helper.

Apply after configure, before a clean build. Patch 0002 modifies the generated
pthread.h; do not run configure again afterwards without reapplying it.

The context-switch and once changes are guarded for __amix__. The public
timespec change also excludes the library's private pthread compilation.

See ../docs/BUILD-RECIPE.md and ../docs/GNU-PTH.md for provenance, commands,
measured failures, successful regressions, and remaining limits.
