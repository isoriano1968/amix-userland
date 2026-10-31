#!/bin/bash
# test-compat-host.sh -- run the compat formatter's battery on THIS machine.
#
# The formatter is ordinary portable C, so its logic can be tested natively in
# a second rather than over a TFTP round trip to the Amiga. Only usleep is
# target-specific, and it is excluded here.
#
# This is the same argument as writing the X11 video driver on Linux first: the
# Amiga is for questions only the Amiga can answer.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/hosttest
cc -O1 -g -Wall -Wextra -Wno-unused-parameter -DAMIX_COMPAT_HOST_TEST \
   -o build/hosttest/compat probe/compat.c compat/amix_compat.c
exec ./build/hosttest/compat
