#!/bin/sh
# configure-amix.sh SRC -- OpenSSL for AMIX: no assembler, no threads; the rest of
# what a 1991 libc has not is in files/include and files/amix-ssl.c
H=$(cd "$(dirname "$0")" && pwd)
cd "$1"
./Configure linux-generic32 no-asm no-threads no-async no-afalgeng no-dso no-engine no-tests \
	no-apps no-ui-console no-dtls no-quic no-dgram no-sctp no-comp no-secure-memory no-devcryptoeng no-ktls no-shared \
	--cross-compile-prefix=m68k-elf- --prefix=/usr/local --openssldir=/usr/local/ssl \
	-m68060 -fPIC -fcommon -std=gnu99 -U__linux__ -Ulinux -D__amix__ -DSVR4 -w \
	-Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types \
	-Dsocklen_t=int -Din_addr_t=__UINT32_TYPE__ -DOPENSSL_USE_USLEEP -DNO_SYSLOG -DOPENSSL_NO_SYSLOG \
	-fno-split-wide-types -I"$H/include"
