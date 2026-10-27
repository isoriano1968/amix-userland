#!/bin/sh
# fetch.sh -- download the Motif and CDE release tarballs into $DL and check
# them against the checksums the patches were made for.
set -e
cd "$(dirname "$0")"
. scripts/common.sh
need sha256sum
mkdir -p "$DL"
get() {     # file sha256 url
	if [ -f "$DL/$1" ] && echo "$2  $DL/$1" | sha256sum -c --quiet - 2>/dev/null; then
		echo "have $1"; return
	fi
	echo "fetching $1"
	if command -v curl >/dev/null; then curl -fL -o "$DL/$1.part" "$3"; else wget -O "$DL/$1.part" "$3"; fi
	echo "$2  $DL/$1.part" | sha256sum -c --quiet - || { echo "$1: checksum mismatch" >&2; exit 1; }
	mv "$DL/$1.part" "$DL/$1"
}
get motif-2.3.8.tar.gz 859b723666eeac7df018209d66045c9853b50b4218cecadb794e2359619ebce7 \
	"https://sourceforge.net/projects/motif/files/Motif%202.3.8%20Source%20Code/motif-2.3.8.tar.gz/download"
get cde-src-2.2.4a.tar.gz 63db1628daecedbe9768ef0ef8079ae02ecdd8dbd7aca63e47ef480a8e4b8b35 \
	"https://sourceforge.net/projects/cdesktopenv/files/src/cde-src-2.2.4a.tar.gz/download"
