#!/bin/sh
# build.sh -- everything, in order: fetch, CDE, the package.  x11/libxp and desktop/motif first.
set -e
cd "$(dirname "$0")"
./fetch.sh
scripts/build-cde.sh
scripts/package-cde.sh
