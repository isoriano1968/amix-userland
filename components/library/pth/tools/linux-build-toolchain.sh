#!/bin/bash
# Run as the builder user in the dedicated Linux VM after copying sysroot.tar.
# This script never writes the AMIX machine or the Windows source sysroot.
set -eu
repo="$HOME/src/gcc14-cross-amix"
mkdir -p "$HOME/src" "$HOME/sysroots/amix" "$HOME/work" "$HOME/opt"
if [ ! -d "$repo/.git" ]; then
    git clone https://github.com/asokero/gcc14-cross-amix.git "$repo"
fi
cd "$repo"
git checkout --detach 58130fe7a322a19cb4c010a8257444cf1c016a07
# GCC's stdint wrapper and libstdc++ require this upstream compatibility header.
# Only the VM's private sysroot is extended; the Windows source stays untouched.
cp compat/include/stdint.h "$HOME/sysroots/amix/usr/include/stdint.h"
cat > config.sh <<'CONFIG'
AMIX_SYSROOT="$HOME/sysroots/amix"
TC_PREFIX="$HOME/opt/m68k-amix"
TC_TARGET=m68k-elf
TC_WORK="$HOME/work/gcc14-amix"
AMIX_CPU=-mcpu=68060
HOST_ADDR=10.0.2.2
CONFIG
# Fresh VM build trees are deliberately confined under ~/work/gcc14-amix.
set +e
bash toolchain/build-cross.sh --jobs 4
status=$?
printf '%s\n' "$status" > "$HOME/toolchain-build.status"
exit "$status"
