#!/bin/sh
# mkdeb.sh OUTDIR -- the AMIX Installer as a Debian package (Debian, Ubuntu,
# Mint and the like), from `make install`: the same files as the IPS package
# desktop/amix-installer on OpenIndiana and Solaris, under /usr.
#
# Needs dpkg-deb and gzip, nothing from debhelper.  Run by `make deb`, after
# `make build`; writes OUTDIR/amix-installer_<version>-<rev>_all.deb.
#
# The kits, pvmk and the cross assembler and linker are separate packages:
# without them the installer still runs and its plan names what is missing.
set -eu

out=${1:?usage: mkdeb.sh OUTDIR}
here=$(cd "$(dirname "$0")/.." && pwd)
version=${VERSION:?VERSION=}
rev=${DEB_REVISION:-1}
name=amix-installer
make=${MAKE:-make}

mkdir -p "$out"
out=$(cd "$out" && pwd)
root=$out/deb-root
rm -rf "$root"

"$make" -C "$here" install DESTDIR="$root" HOST_PREFIX=/usr >/dev/null

doc=$root/usr/share/doc/$name
mkdir -p "$doc" "$root/DEBIAN"

# copyright in the machine-readable format, with the MIT text
{
    echo "Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/"
    echo "Upstream-Name: $name"
    echo "Source: https://github.com/isoriano1968/amix-userland"
    echo
    echo "Files: *"
    echo "Copyright: 2026 isoriano1968"
    echo "License: MIT"
    sed -e 's/^$/./' -e 's/^/ /' "$here/$name.license"
} > "$doc/copyright"

# changelog: one entry for this version, dated from the last commit
when=$(git -C "$here" log -1 --format=%cD -- . 2>/dev/null || date -R)
{
    echo "$name ($version-$rev) unstable; urgency=medium"
    echo
    echo "  * AMIX Installer $version, see"
    echo "    https://github.com/isoriano1968/amix-userland/commits/main/components/desktop/$name"
    echo
    echo " -- isoriano1968 <81116077+isoriano1968@users.noreply.github.com>  $when"
} | gzip -9n > "$doc/changelog.Debian.gz"

# files 644, directories and the launcher 755
chmod 755 "$root" "$root/DEBIAN"
find "$root/usr" -type d -exec chmod 755 {} +
find "$root/usr" -type f -exec chmod 644 {} +
chmod 755 "$root/usr/bin/$name"

size=$(du -sk "$root/usr" | cut -f1)
cat > "$root/DEBIAN/control" <<EOF
Package: $name
Version: $version-$rev
Architecture: all
Maintainer: isoriano1968 <81116077+isoriano1968@users.noreply.github.com>
Installed-Size: $size
Depends: python3 (>= 3.9), python3-gi, gir1.2-gtk-4.0 (>= 4.12), gir1.2-adw-1 (>= 1.4)
Recommends: fs-uae, xdg-user-dirs
Section: otherosfs
Priority: optional
Homepage: https://github.com/isoriano1968/amix-userland
Description: makes Amiga UNIX (AMIX) disks for an Amiga or FS-UAE
 A GNOME app (GTK 4, libadwaita) that makes an AMIX disk for one machine
 from the user's own AMIX 2.1c core system: a core HDF, a tape image with
 its root floppy, or a running AMIX system.  It adds the kernel, drivers,
 desktop and tools from amix-userland and amix-gate that fit the machine,
 and writes an FS-UAE configuration.  It never ships Commodore's files.
 .
 The kernel driver kits, the pvmk boot loader and the m68k cross assembler
 and linker come from their own packages; the plan names any that are
 missing.
EOF

( cd "$root" && find usr -type f -exec md5sum {} + | sort -k2 ) > "$root/DEBIAN/md5sums"
chmod 644 "$root/DEBIAN/control" "$root/DEBIAN/md5sums"

deb=$out/${name}_$version-${rev}_all.deb
dpkg-deb --root-owner-group -Zxz --build "$root" "$deb" >/dev/null
rm -rf "$root"
echo "$deb"
