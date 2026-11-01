#!/bin/sh
# build.sh -- everything, in order: fetch, libXp, CDE, the package.  desktop/motif first.
set -e
cd "$(dirname "$0")"
./fetch.sh
scripts/build-libxp.sh
scripts/build-cde.sh
scripts/package-cde.sh
