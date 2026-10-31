#!/bin/sh
# Run as root from an expanded, locally built AMIX installation archive.
set -e
umask 022
stage=`pwd`
documentation=/usr/local/share/doc/python3-amix
manifest="$stage/payload$documentation/INSTALL-FILES"
links="$stage/payload$documentation/INSTALL-LINKS"
case "`id`" in uid=0\(*) ;; *) echo 'Run install.sh as root' >&2; exit 1;; esac
test -r "$manifest"
test -r "$links"
# Inspect the entire payload before changing any installed file.
while read destination; do
    case "$destination" in
        /usr/local/gnu/*|*/../*|*/./*|*//*) exit 2;;
        /usr/local/*) ;; *) exit 2;;
    esac
    test -f "$stage/payload$destination"
    case "$destination" in *' '*|*'\'*) exit 2;; esac
    if test -f "$destination"; then
        case "`ls -ld "$destination"`" in -*) ;; *) exit 2;; esac
        if cmp "$stage/payload$destination" "$destination" >/dev/null 2>&1; then
            :
        else
            echo "Refusing to replace differing existing file: $destination" >&2
            exit 1
        fi
    elif ls -ld "$destination" >/dev/null 2>&1; then
        echo "Refusing non-regular destination: $destination" >&2
        exit 1
    fi
done < "$manifest"
while read destination target kind; do
    case "$destination:$target:$kind" in
        /usr/local/bin/python3:python3.11:|\
        /usr/local/bin/python3-config:python3.11-config:hard|\
        /usr/local/man/man1/python3.1:python3.11.1:|\
        /usr/local/lib/python3.11/config-3.11/libpython3.11.a:../../libpython3.11.a:) ;;
        *) exit 2;;
    esac
    if ls -ld "$destination" >/dev/null 2>&1; then
        if test "$kind" = hard; then
            directory=`dirname "$destination"`
            ls -id "$destination" "$directory/$target" | awk \
                'NR==1 {inode=$1} NR==2 {if ($1!=inode) bad=1} END {exit (bad || NR!=2)}'
        else
            ls -ld "$destination" | awk \
                '$(NF-1)=="->" && $NF==expected {ok=1} END {exit !ok}' expected="$target"
        fi
    fi
done < "$links"
while read destination; do
    if test -f "$destination"; then :
    else
        directory=`dirname "$destination"`
        test -d "$directory" || mkdir -p "$directory"
        echo "Installing $destination"
        cp "$stage/payload$destination" "$destination"
    fi
done < "$manifest"
# Bound argument batches for the old AMIX exec limit. Manifest paths have no
# whitespace/backslashes; applying metadata in batches avoids thousands of forks.
xargs -n16 chmod 644 < "$manifest"
xargs -n16 chown root < "$manifest"
xargs -n16 chgrp bin < "$manifest"
chmod 755 /usr/local/bin/python3.11 /usr/local/bin/python3.11-config
for directory in /usr/local/lib/python3.11/site-packages /usr/local/lib/python3.11/lib-dynload; do
    test -d "$directory" || mkdir -p "$directory"
done
while read destination target kind; do
    if ls -ld "$destination" >/dev/null 2>&1; then :
    elif test "$kind" = hard; then
        directory=`dirname "$destination"`
        ln "$directory/$target" "$destination"
    else ln -s "$target" "$destination"
    fi
done < "$links"
while read destination; do
    cmp "$stage/payload$destination" "$destination"
done < "$manifest"
while read destination target kind; do
    if test "$kind" = hard; then
        directory=`dirname "$destination"`
        ls -id "$destination" "$directory/$target" | awk \
            'NR==1 {inode=$1} NR==2 {if ($1!=inode) bad=1} END {exit (bad || NR!=2)}'
    else
        ls -ld "$destination" | awk \
            '$(NF-1)=="->" && $NF==expected {ok=1} END {exit !ok}' expected="$target"
    fi
done < "$links"
count=`awk 'END {print NR}' "$manifest"`
echo "Python 3.11 installed under /usr/local; $count files and 4 links verified"
echo 'Until the random driver is enabled, explicitly set PYTHONHASHSEED=0 when running Python.'
