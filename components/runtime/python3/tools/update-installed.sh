#!/bin/sh
# update-installed.sh STEP -- replace an installed Python 3.11 in /usr/local
# with a new payload, beside the old one first.  Run as root on AMIX from the
# directory holding the expanded payload/ (on the same file system as
# /usr/local, so that every step is a rename).  install.sh is for a first
# installation; this is for an update.  Back the old installation up first.
#
#   prepare   payload/<path> -> <path>.new for each part below
#   verify    sha256 of every .new file against the new SHA256SUMS, with python3
#   swap      <path> -> <path>.old, <path>.new -> <path>; links, owners, modes
#   rollback  undo swap: <path> -> <path>.bad, <path>.old -> <path>
#   clean     remove the <path>.old copies once the new Python is tested
#
# The original AMIX sh has no `!', so tests are written with || and if/else.
set -e
umask 022
stage=`pwd`
doc=/usr/local/share/doc/python3-amix
config=/usr/local/lib/python3.11/config-3.11
parts="/usr/local/lib/python3.11 /usr/local/include/python3.11 $doc
/usr/local/bin/python3.11 /usr/local/bin/python3.11-config
/usr/local/lib/libpython3.11.a /usr/local/man/man1/python3.11.1"
case "`id`" in uid=0\(*) ;; *) echo 'Run as root' >&2; exit 1;; esac
there() { ls -ld "$1" >/dev/null 2>&1; }

case "$1" in
prepare)
    for p in $parts; do
        test -r "$stage/payload$p" || { echo "missing payload$p" >&2; exit 1; }
        if there "$p.new"; then echo "$p.new exists" >&2; exit 1; fi
    done
    for p in $parts; do mv "$stage/payload$p" "$p.new"; done
    echo 'prepared: each part is beside the installed one as .new'
    ;;
verify)
    /usr/local/bin/python3 - "$doc.new/SHA256SUMS" <<'EOF'
import hashlib, sys
parts = ['/usr/local/lib/python3.11/', '/usr/local/include/python3.11/',
         '/usr/local/share/doc/python3-amix/']
good = bad = 0
for line in open(sys.argv[1]):
    digest, path = line.split()
    for part in parts:
        if path.startswith(part):
            path = part[:-1] + '.new/' + path[len(part):]
            break
    else:
        path += '.new'
    with open(path, 'rb') as f:
        if hashlib.sha256(f.read()).hexdigest() == digest:
            good += 1
        else:
            bad += 1
            print('MISMATCH', path)
print('sha256 verified: %d good, %d bad' % (good, bad))
sys.exit(1 if bad or not good else 0)
EOF
    ;;
swap)
    for p in $parts; do
        there "$p.new" || { echo "no $p.new" >&2; exit 1; }
        if there "$p.old"; then echo "$p.old exists" >&2; exit 1; fi
    done
    for p in $parts; do
        if there "$p"; then mv "$p" "$p.old"; fi
        mv "$p.new" "$p"
    done
    # packages the user installed stay installed
    if test -d /usr/local/lib/python3.11.old/site-packages; then
        for f in `ls -a /usr/local/lib/python3.11.old/site-packages`; do
            case "$f" in .|..|README.txt) ;;
            *) mv "/usr/local/lib/python3.11.old/site-packages/$f" /usr/local/lib/python3.11/site-packages/;;
            esac
        done
    fi
    for d in /usr/local/lib/python3.11/site-packages /usr/local/lib/python3.11/lib-dynload; do
        test -d "$d" || mkdir "$d"
    done
    there $config/libpython3.11.a || ln -s ../../libpython3.11.a $config/libpython3.11.a
    # the kernel runs a shell script badly through a relative symlink: a hard link
    rm -f /usr/local/bin/python3-config
    ln /usr/local/bin/python3.11-config /usr/local/bin/python3-config
    there /usr/local/bin/python3 || ln -s python3.11 /usr/local/bin/python3
    there /usr/local/man/man1/python3.1 || ln -s python3.11.1 /usr/local/man/man1/python3.1
    for p in $parts; do
        /usr/gnu/bin/find "$p" -type d -exec chmod 755 {} +
        /usr/gnu/bin/find "$p" -type f -exec chmod 644 {} +
        /usr/gnu/bin/find "$p" -exec chown root {} + -exec chgrp bin {} +
    done
    chmod 755 /usr/local/bin/python3.11 /usr/local/bin/python3.11-config
    echo 'swapped: the new Python is installed, the old one is in the .old copies'
    ;;
rollback)
    for p in $parts; do
        if there "$p.old"; then
            if there "$p"; then mv "$p" "$p.bad"; fi
            mv "$p.old" "$p"
        fi
    done
    rm -f /usr/local/bin/python3-config
    ln /usr/local/bin/python3.11-config /usr/local/bin/python3-config
    echo 'rolled back: the old Python is installed again, the new one is in .bad'
    ;;
clean)
    for p in $parts; do
        if there "$p.old"; then rm -rf "$p.old"; fi
    done
    echo 'old copies removed'
    ;;
*)
    echo 'usage: update-installed.sh prepare|verify|swap|rollback|clean' >&2
    exit 2
    ;;
esac
