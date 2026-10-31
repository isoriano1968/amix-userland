#!/bin/sh
# build.sh -- everything, in order: fetch, Motif, libXp, CDE, the packages.
set -e
cd "$(dirname "$0")"
./fetch.sh
scripts/build-motif.sh
scripts/build-libxp.sh
scripts/build-cde.sh
scripts/package-motif.sh
scripts/package-cde.sh
