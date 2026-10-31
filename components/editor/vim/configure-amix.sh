#!/bin/sh
# configure-amix.sh -- vim 9.1 for AMIX (SVR4.0, m68k) with gcc14-cross-amix
# and ncurses 6.6 from the sysroot (usr/local).  Terminal vim only, "normal"
# features.  The run-time tests a cross build cannot do are answered in the
# environment below (vim_cv_*).
H=$(cd "$(dirname "$0")" && pwd)
SR=$HOME/opt/m68k-amix-sysroot
export PATH=$HOME/opt/m68k-amix/bin:$PATH
W="-Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=return-mismatch -Wno-error=implicit-int"
cd "$H/vim-9.1.0000/src"
export vim_cv_toupper_broken=no vim_cv_terminfo=yes vim_cv_tgetent=zero \
	vim_cv_getcwd_broken=no vim_cv_stat_ignores_slash=no \
	vim_cv_memmove_handles_overlap=yes vim_cv_bcopy_handles_overlap=yes \
	vim_cv_memcpy_handles_overlap=no vim_cv_tty_group=world vim_cv_tty_mode=0620 \
	vim_cv_uname_output=AMIX vim_cv_uname_r_output=4.0 vim_cv_uname_m_output=m68k
CC=m68k-elf-gcc \
CFLAGS="-O2 -U__linux__ -Ulinux -D__amix__ -D__svr4__ -DSVR4 -fcommon $W" \
CPPFLAGS="-I$SR/usr/local/include" LDFLAGS="-L$SR/usr/local/lib -Wl,-R,/usr/local/lib" \
	./configure --host=m68k-elf --build=x86_64-pc-linux-gnu --prefix=/usr/local \
	--with-features=normal --disable-gui --without-x --enable-gpm=no \
	--disable-nls --disable-netbeans --disable-channel --disable-sysmouse \
	--disable-canberra --disable-libsodium --disable-xsmp --disable-selinux \
	--with-tlib=ncurses --disable-acl --disable-smack
