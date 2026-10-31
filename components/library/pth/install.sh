#!/bin/sh
# Simple GNU Pth installation. Run as root from the expanded staging archive.
set -e
umask 022
prefix=/usr/local/gnu
stage=`pwd`
manifest="$stage/payload$prefix/share/doc/gnu-pth/INSTALL-FILES"
test -r "$manifest"
case "`id`" in uid=0\(*) ;; *) echo 'Run install.sh as root' >&2; exit 1;; esac
# Preflight every file before changing the destination. Identical installs are
# safe to repeat; differing existing files require an explicit backup/upgrade.
while read destination; do
    case "$destination" in /usr/local/gnu/*) ;; *) exit 2;; esac
    test -f "$stage/payload$destination"
    if ls -ld "$destination" >/dev/null 2>&1; then
        if cmp "$stage/payload$destination" "$destination" >/dev/null 2>&1; then
            :
        else
            echo "Refusing to replace differing existing file: $destination" >&2
            exit 1
        fi
    fi
done < "$manifest"
while read destination; do
    directory=`dirname "$destination"`
    test -d "$directory" || mkdir -p "$directory"
    cp "$stage/payload$destination" "$destination"
    case "$destination" in /usr/local/gnu/bin/*) chmod 755 "$destination";;
        *) chmod 644 "$destination";; esac
    chown root "$destination"
    chgrp bin "$destination"
done < "$manifest"
while read destination; do
    cmp "$stage/payload$destination" "$destination"
done < "$manifest"
# The original AMIX Bourne shell runs redirected loops in a subshell.
count=`awk 'END {print NR}' "$manifest"`
test "$count" -eq 15
/usr/local/gnu/bin/pthread-config --version
/usr/local/gnu/bin/pthread-config --prefix --includedir --libdir --cflags --ldflags --libs
echo "GNU Pth installed under $prefix; $count files verified"
