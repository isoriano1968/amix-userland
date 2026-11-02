#!/bin/sh

set -e

BASE_URL=https://www.x.org/archive/X11R6.3/tars
ARCHIVES="xc-1.tar.gz xc-2.tar.gz xc-3.tar.gz"

SCRIPT_DIR=`dirname "$0"`
REPO_DIR=`cd "$SCRIPT_DIR" && pwd`
DEST=${1-`pwd`/work}
CACHE=${X11R63_DISTFILES-$REPO_DIR/distfiles}

fail()
{
    echo "install.sh: $*" >&2
    exit 1
}

fetch()
{
    url=$1
    output=$2

    if command -v curl >/dev/null 2>&1
    then
        curl -fL "$url" -o "$output"
    elif command -v wget >/dev/null 2>&1
    then
        wget -O "$output" "$url"
    else
        fail "curl or wget is required to download X11R6.3"
    fi
}

verify()
{
    expected=$1
    file=$2

    if command -v sha256sum >/dev/null 2>&1
    then
        echo "$expected  $file" | sha256sum -c - >/dev/null
    elif command -v shasum >/dev/null 2>&1
    then
        echo "$expected  $file" | shasum -a 256 -c - >/dev/null
    elif command -v digest >/dev/null 2>&1
    then
        actual=`digest -a sha256 "$file"`
        test "$actual" = "$expected"
    else
        fail "sha256sum, shasum, or digest is required"
    fi
}

if test -e "$DEST/xc"
then
    fail "$DEST/xc already exists; choose a new destination"
fi

mkdir -p "$CACHE" "$DEST"

while read expected archive
do
    case "$archive" in
    ''|'#'*) continue ;;
    esac

    file=$CACHE/$archive
    if test -s "$file"
    then
        :
    else
        echo "Downloading $archive"
        fetch "$BASE_URL/$archive" "$file.tmp"
        mv "$file.tmp" "$file"
    fi

    echo "Verifying $archive"
    verify "$expected" "$file" || fail "checksum mismatch: $archive"
done < "$REPO_DIR/SOURCES.sha256"

for archive in $ARCHIVES
do
    echo "Extracting $archive"
    gzip -dc "$CACHE/$archive" | (cd "$DEST" && tar xf -)
done

test -f "$DEST/xc/Imakefile" || fail "X11R6.3 extraction is incomplete"

echo "Applying the AMIX patches"
for p in "$REPO_DIR"/patches/*.patch
do
    (cd "$DEST" && patch -s -p1 < "$p" 2>&1 | grep -v 'is read-only; trying to patch anyway') || true
done
U=$DEST/xc/config/util
chmod +x "$U/amix-build-programs.sh" "$U/amix-shared-rebuild.sh" "$U/startXzz9000" \
    "$U/xinitrc.zz9000" "$U/gears.zz9000"

test -f "$DEST/xc/config/cf/amix.cf" || fail "AMIX patches were not applied"
test -f "$DEST/xc/programs/Xserver/hw/amix/rtg/zz9000/zz9000hw.c" || \
    fail "ZZ9000 DDX was not patched in"

echo "X11R6.3 AMIX source tree prepared at $DEST/xc"
echo "Copy that xc tree to /usr/x11r6/xc on AMIX before building."
