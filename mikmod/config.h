/* config.h - hand-written for the AMIX (Commodore Amiga UNIX, m68k SVR4)
   port, for the same reason as usr/local/src/libmikmod/config.h: upstream's
   autoconf/automake build system has no knowledge of the m68k-cbm-sysv4
   host, and there's no autotools available anywhere in this workflow to
   regenerate it. See usr/local/src/libmikmod/README.md for the fuller
   explanation; this file follows the same approach, checked against what
   actually exists under AMIX's usr/include and usr/lib. */

/* ---- Headers available on AMIX ---- */
#define HAVE_CURSES_H 1      /* ncurses 5.4, installed as plain curses.h */
#define HAVE_FCNTL_H 1
#define HAVE_LIMITS_H 1
#define HAVE_MEMORY_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_WAIT_H 1
#define HAVE_TERMIOS_H 1
#define HAVE_UNISTD_H 1

/* Headers NOT present on AMIX -- left undefined: HAVE_STRINGS_H,
   HAVE_INTTYPES_H, HAVE_STDINT_H, HAVE_FNMATCH_H, HAVE_SCHED_H (unused
   outside __linux anyway),  HAVE_NCURSES_H, HAVE_NCURSES_CURSES_H. */

/* TIOCGWINSZ lives in <sys/termios.h> on AMIX, pulled in via <termios.h>
   -- NOT visible from <sys/ioctl.h> alone. Leaving this undefined makes
   mwindow.c additionally #include <termios.h>, which is what we need. */
#undef GWINSZ_IN_SYS_IOCTL

/* ANSI C headers all present */
#define STDC_HEADERS 1

/* No fnmatch(), mkstemp(), snprintf(), vsnprintf(), srandom(), usleep(),
   or getopt_long_only() in AMIX's SVR4 libc -- leave all of
   HAVE_FNMATCH, HAVE_MKSTEMP, HAVE_SNPRINTF, HAVE_VSNPRINTF, HAVE_SRANDOM,
   HAVE_USLEEP, HAVE_USLEEP_PROTO, HAVE_GETOPT_LONG_ONLY undefined.
   mfnmatch.c, mutilities.c's m_mkstemp()/mik_snprintf(), musleep.c's
   usleep_new(), and getopt_long.c already provide portable fallbacks for
   all of these -- see Makefile, which compiles those extra sources in. */

/* No pthreads -- single-threaded build, matching libmikmod's own config.h.
   The player already handles this: see src/mplayer.c's MP_Update(),
   called from the main event loop instead of a background thread when
   USE_THREADS ends up undefined (mthreads.h). */

/* ncurses 5.4 here predates resizeterm() (added in ncurses 4.2+, but only
   reliably present/linkable from later releases); leave undefined so
   mwindow.c falls back to its own resize handling. */
#undef HAVE_NCURSES_RESIZETERM

/* Not AIX */
#undef MIKMOD_AIX

/* O_BINARY (DOS/Windows text-vs-binary open mode) doesn't exist on Unix at
   all -- mutilities.c and marchive.c use it unconditionally without a
   guard. Unix has no such distinction, so this is always a no-op. */
#ifndef O_BINARY
#define O_BINARY 0
#endif

/* AMIX's <sys/stat.h> defines the S_IFLNK/S_IFSOCK mode bits but not the
   S_ISLNK()/S_ISSOCK() convenience macros mlistedit.c calls -- only
   S_ISDIR/S_ISREG are provided. Build them from the bits that do exist. */
#ifndef S_ISLNK
#define S_ISLNK(mode) (((mode) & 0xF000) == S_IFLNK)
#endif
#ifndef S_ISSOCK
#define S_ISSOCK(mode) (((mode) & 0xF000) == S_IFSOCK)
#endif

/* mlist.c calls random()/srandom() unconditionally (playlist shuffle),
   without the HAVE_SRANDOM guard libmikmod's own code uses for the same
   thing -- substitute the always-present ANSI rand()/srand(). */
#ifndef HAVE_SRANDOM
#define random rand
#define srandom srand
#endif

/* Default location for the system-wide mikmodrc, matching this port's
   usr/local convention (see usr/local/src/libmikmod/README.md). The
   Makefile's install target copies the shipped mikmodrc there. */
#define PACKAGE_DATA_DIR "/usr/local/share/mikmod"

/* Package metadata (normally filled in by AC_INIT/AM_INIT_AUTOMAKE) */
#define PACKAGE "mikmod"
#define PACKAGE_NAME "mikmod"
#define PACKAGE_TARNAME "mikmod"
#define PACKAGE_VERSION "3.2.9"
#define PACKAGE_STRING "mikmod 3.2.9"
#define PACKAGE_BUGREPORT ""
#define PACKAGE_URL ""
#define VERSION "3.2.9"
