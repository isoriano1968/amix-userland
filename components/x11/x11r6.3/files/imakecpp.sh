#!/bin/sh
# imakecpp.sh -- the build host's C preprocessor for imake, for the AMIX
# cross configuration.  Same rule as amix-z3660rtg's x11/cross/imakecpp.sh:
# no host predefines (-undef), and the host imake's own -Dlinux (compiled in
# from its host build) dropped, so Imake.cf selects amix.cf only.
#
# The preprocessor is HOST_CPP (default: cpp from PATH, which is what the
# original's /usr/bin/cpp is on a Linux host), not a fixed path: OpenIndiana
# has no cpp in /usr/bin, so the component Makefile sets HOST_CPP to the
# host gcc's cpp there.
args=""
for a in "$@"; do
	case "$a" in
	-Dlinux|-D__linux__|-Dunix|-Dsun|-D__sun|-D__sun__) ;;
	*) args="$args \"$a\"" ;;
	esac
done
eval exec ${HOST_CPP:-cpp} -undef $args
