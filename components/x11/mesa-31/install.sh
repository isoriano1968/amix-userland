#!/bin/sh

set -e

BASE_URL=https://archive.mesa3d.org/older-versions/3.x
ARCHIVES="MesaLib-3.1.tar.gz MesaDemos-3.1.tar.gz"

SCRIPT_DIR=`dirname "$0"`
REPO_DIR=`cd "$SCRIPT_DIR" && pwd`
DEST=${1-`pwd`/work}
CACHE=${MESA31_DISTFILES-$REPO_DIR/distfiles}

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
        fail "curl or wget is required to download Mesa 3.1"
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

if test -e "$DEST/Mesa-3.1"
then
    fail "$DEST/Mesa-3.1 already exists; choose a new destination"
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

ROOT=$DEST/Mesa-3.1
test -f "$ROOT/Make-config" || fail "Mesa 3.1 extraction is incomplete"

echo "Installing AMIX compiler configuration"
cp "$REPO_DIR/config/Make-config.amix" "$ROOT/Make-config"

echo "Installing AMIX build files"
(cd "$REPO_DIR/overlay" && tar cf - Mesa-3.1) | (cd "$DEST" && tar xpf -)

test -f "$ROOT/amix-build.sh" || fail "AMIX overlay was not installed"
grep 'CC = gcc' "$ROOT/Make-config" >/dev/null || \
    fail "AMIX compiler configuration was not installed"
echo "Mesa 3.1 AMIX source tree prepared at $ROOT"
echo "Copy that tree to /usr/mesa3/Mesa-3.1 on AMIX before building."
