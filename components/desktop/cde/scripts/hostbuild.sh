#!/bin/sh
# hostbuild.sh DIR TARGET... -- build CDE build-time tools in $CDE/DIR with the host
# compiler (they run on the build host during the build); the cross settings
# from host.def are overridden on the make command line.  CLEAN="files" are
# removed first (a directory that holds only host tools: CLEAN="*.o *.a").
D=$1; shift
cd "${CDE:?}/$D" || exit 1
for t in "$@"; do rm -f "$t"; done
[ -n "$CLEAN" ] && rm -f $CLEAN
make CC="gcc -std=gnu89 -fcommon -w" CCLINK="gcc -std=gnu89 -fcommon -w" \
	CXX="g++ -fpermissive -w" CXXLINK="g++ -fpermissive -w" \
	CCOPTIONS= CXXOPTIONS= CDEBUGFLAGS=-O CXXDEBUGFLAGS=-O \
	STD_DEFINES="-DANSICPP -DMULTIBYTE -DNLS16" \
	EXTRA_LIBRARIES= LDPRELIB= LDPRELIBS= LDPOSTLIB= \
	XLIB=-lX11 REGEXSYSLIB= DYNLIBSYSLIB= ICONVSYSLIB= \
	AR="ar cq" RANLIB=ranlib "$@"
