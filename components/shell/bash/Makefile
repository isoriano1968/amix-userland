# GNU bash 5.2.37 for AMIX, in /usr/gnu/bin, with readline on ncurses.
COMPONENT_NAME    = bash
COMPONENT_VERSION = 5.2.37
COMPONENT_ARCHIVE = bash-5.2.37.tar.gz
COMPONENT_URL     = https://mirrors.kernel.org/gnu/bash/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 9599b22ecd1d5787ad7d3b7bf0c59f312b3396d1e281175dd1f8a4014da621ff
CLEAN_FILES       = bash-5.2.37 $(COMPONENT_ARCHIVE)
PROGS             = bash
EXTRA_CONF        = --without-bash-malloc --disable-nls --disable-multibyte --enable-readline --enable-history --without-installed-readline
EXTRA_ENV         = bash_cv_type_socklen_t=yes ac_cv_func_gethostname=yes bash_cv_termcap_lib=libncurses
EXTRA_LIBS        = $(shell $(PKG_CONFIG) --libs ncurses)
MAKE_TARGET       = bash
EXTRA_CFLAGS      = -DAMIX_RUSAGE -Dsocklen_t=int -include signal.h
include ../../gnu/gnu.mk
