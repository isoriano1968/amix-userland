#!/bin/sh
# fetch.sh -- download SDL 2.30.12 and unpack it here (SDL2-2.30.12/).
set -e
cd "$(dirname "$0")"
V=2.30.12
[ -d SDL2-$V ] && { echo "SDL2-$V/ is there"; exit 0; }
curl -fLO https://github.com/libsdl-org/SDL/releases/download/release-$V/SDL2-$V.tar.gz
echo "ac356ea55e8b9dd0b2d1fa27da40ef7e238267ccf9324704850d5d47375b48ea  SDL2-$V.tar.gz" | sha256sum -c
tar xzf SDL2-$V.tar.gz
