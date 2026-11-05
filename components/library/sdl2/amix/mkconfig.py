#!/usr/bin/env python3
"""mkconfig.py SDL_config.h.cmake > SDL_config.h -- SDL's configuration for AMIX.

AMIX 2.1 (SVR4.0) on a 68040/68060 with gcc14-cross-amix, X11R6.3 and
gnu-pth-amix: what is set here is what AMIX has; everything else stays
undefined and SDL uses its own code (SDL_iconv, SDL_strtoll, ...).
nanosleep, poll and select go through gnu-pth's pthread.h, which every SDL
file includes (build.sh), so a thread that sleeps or waits lets the
others run.
"""
import re, sys

ON = """
HAVE_GCC_ATOMICS HAVE_LIBC
HAVE_CTYPE_H HAVE_FLOAT_H HAVE_LIMITS_H HAVE_MATH_H HAVE_MEMORY_H HAVE_SIGNAL_H
HAVE_STDARG_H HAVE_STDDEF_H HAVE_STDINT_H HAVE_STDIO_H HAVE_STDLIB_H HAVE_STRING_H
HAVE_SYS_TYPES_H
HAVE_MALLOC HAVE_CALLOC HAVE_REALLOC HAVE_FREE HAVE_ALLOCA HAVE_GETENV
HAVE_QSORT HAVE_BSEARCH HAVE_ABS HAVE_MEMSET HAVE_MEMCPY HAVE_MEMMOVE HAVE_MEMCMP
HAVE_STRLEN HAVE_STRCHR HAVE_STRRCHR HAVE_STRSTR HAVE_STRTOL HAVE_STRTOUL HAVE_STRTOD
HAVE_ATOI HAVE_ATOF HAVE_STRCMP HAVE_STRNCMP HAVE_SSCANF HAVE_VSNPRINTF
HAVE_ACOS HAVE_ASIN HAVE_ATAN HAVE_ATAN2 HAVE_CEIL HAVE_CEILF HAVE_COS
HAVE_COSF HAVE_EXP HAVE_EXPF HAVE_FABS HAVE_FABSF HAVE_FLOOR HAVE_FLOORF HAVE_FMOD
HAVE_LOG HAVE_LOGF HAVE_LOG10 HAVE_POW HAVE_POWF HAVE_SIN HAVE_SINF HAVE_SQRT HAVE_SQRTF
HAVE_TAN HAVE_ATAN2F HAVE_ATANF
HAVE_SIGACTION HAVE_SETJMP HAVE_NANOSLEEP HAVE_SYSCONF HAVE_POLL
SDL_AUDIO_DRIVER_DUMMY SDL_AUDIO_DRIVER_AMIX SDL_AUDIO_DRIVER_ZZHDMI
SDL_JOYSTICK_DISABLED SDL_HAPTIC_DISABLED SDL_HIDAPI_DISABLED SDL_SENSOR_DISABLED
SDL_LOADSO_DISABLED SDL_POWER_DISABLED
SDL_THREAD_PTHREAD SDL_THREAD_PTHREAD_RECURSIVE_MUTEX
SDL_TIMER_UNIX
SDL_VIDEO_DRIVER_X11 SDL_VIDEO_DRIVER_DUMMY
SDL_FILESYSTEM_UNIX SDL_MISC_DUMMY SDL_LOCALE_DUMMY
DYNAPI_NEEDS_DLOPEN
""".split()

on = set(ON)
out = []
for line in open(sys.argv[1]):
    m = re.match(r'#cmakedefine01\s+(\w+)', line)
    if m:
        out.append("#define %s %d\n" % (m.group(1), 1 if m.group(1) in on else 0))
        continue
    m = re.match(r'#cmakedefine\s+(\w+)', line)
    if m:
        name = m.group(1)
        out.append(("#define %s 1\n" % name) if name in on else ("/* #undef %s */\n" % name))
        on.discard(name)
        continue
    if line.startswith('#if @'):		# e.g. the assertion level: not configured
        out.append('#if 0\n')
        continue
    out.append(re.sub(r'@\w+@', '', line))
text = "".join(out)
# our own driver is not in SDL's template
extra = "\n/* AMIX additions (sdl2-amix) */\n" + "".join("#define %s 1\n" % n for n in sorted(on))
text = text.replace("#endif /* SDL_config_h_ */", extra + "\n#endif /* SDL_config_h_ */")
sys.stdout.write(text)
