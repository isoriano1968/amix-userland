#!/bin/sh
# build-shared-cde.sh STAGE -- CDE on shared libraries, in work-shared/: a full
# build with -fPIC, against the shared X and Motif in STAGE
# (x11/x11r6.3 build-shared.sh, then build-shared-motif.sh), which takes the
# shared libraries into STAGE/usr/dt/lib.  work/ stays as it is.
set -e
TOP=$(cd "$(dirname "$0")/.." && pwd)
export SHARED_WORK=$TOP/work-shared CDE_SHARED=$1
mkdir -p "$SHARED_WORK"
for d in motif-root motif-host libxp motif-2.3.8; do
	ln -sfn "$TOP/work/$d" "$SHARED_WORK/$d"
done
sh "$TOP/scripts/build-cde.sh" "$@"
