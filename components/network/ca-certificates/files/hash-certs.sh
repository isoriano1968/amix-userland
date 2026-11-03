#!/bin/sh
# hash-certs.sh BUNDLE DIR -- the bundle as one file per certificate, named by
# OpenSSL's subject hash (what "openssl rehash" makes): OpenSSL then reads
# only the certificates a connection needs, instead of parsing all of them
# (3.6 s on the 060).  Needs the PC's openssl 3.
set -e
mkdir -p "$2"
awk -v d="$2" '/BEGIN CERTIFICATE/ { n++; f = sprintf("%s/tmp%03d.pem", d, n) }
	f { print > f }
	/END CERTIFICATE/ { close(f); f = "" }' "$1"
for f in "$2"/tmp*.pem; do
	h=$(openssl x509 -noout -subject_hash -in "$f")
	i=0
	while [ -e "$2/$h.$i" ]; do i=$((i + 1)); done
	mv "$f" "$2/$h.$i"
	chmod 644 "$2/$h.$i"
done
