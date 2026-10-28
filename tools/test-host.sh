#!/bin/sh
# Logic test on Linux. Host results are never AMIX hardware acceptance.
set -eu
mkdir -p build
gcc -O2 -std=gnu89 -Wall -Wextra -D_POSIX_C_SOURCE=2 -Ipoc \
    poc/pthread.c poc/proof.c -o build/host-proof
echo 'Linux host logic test (not AMIX acceptance)'
./build/host-proof
gcc -O2 -std=gnu89 -Wall -Wextra -D_POSIX_C_SOURCE=2 -Ipoc \
    poc/pthread.c poc/limits.c -o build/host-limits
./build/host-limits
