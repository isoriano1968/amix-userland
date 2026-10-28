"""Apply the measured AMIX errno fix to GNU Pth 2.0.7's mcsc backend.

This modifies the external source tree, not any system library. Keep pristine
*.amix-original files to generate distributable source patches.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1])
path = root / "pth_mctx.c"
text = path.read_text()
old = """#if PTH_MCTX_MTH(mcsc)
#define pth_mctx_switch(old,new) \\
    _pth_mctx_switch_debug \\
    swapcontext(&((old)->uc), &((new)->uc));
"""
new = """#if PTH_MCTX_MTH(mcsc)
#if defined(__amix__)
/* AMIX errno is process-global: preserve it across cooperative switches. */
#define pth_mctx_switch(old,new) \\
    do { \\
        _pth_mctx_switch_debug \\
        (old)->error = errno; \\
        errno = (new)->error; \\
        swapcontext(&((old)->uc), &((new)->uc)); \\
        errno = (old)->error; \\
    } while (0)
#else
#define pth_mctx_switch(old,new) \\
    _pth_mctx_switch_debug \\
    swapcontext(&((old)->uc), &((new)->uc));
#endif
"""
marker = "AMIX errno is process-global"
if marker not in text:
    if text.count(old) != 1:
        raise SystemExit("Unexpected GNU Pth context-switch source")
    backup = path.with_name(path.name + ".amix-original")
    if not backup.exists():
        backup.write_text(text)
    text = text.replace(old, new)
    anchor = "    /* configure startup function (with no arguments) */"
    if text.count(anchor) != 1:
        raise SystemExit("Unexpected GNU Pth mcsc initializer")
    text = text.replace(anchor,
        "#if defined(__amix__)\n    mctx->error = 0;\n#endif\n\n" + anchor)
    path.write_text(text)
print("GNU Pth AMIX errno patch ready")

# Pth already defines this fallback privately, but AMIX applications also need
# a complete public type to call pthread_cond_timedwait or nanosleep.
header = root / "pthread.h"
config = (root / "pth_acdef.h").read_text()
if "#define HAVE_STRUCT_TIMESPEC 1" not in config:
    public = header.read_text()
    marker = "_PTH_AMIX_PUBLIC_TIMESPEC"
    if marker not in public:
        anchor = "struct timespec;"
        if public.count(anchor) != 1:
            raise SystemExit("Unexpected GNU Pth public timespec declaration")
        fallback = """#if defined(__amix__) && !defined(_PTHREAD_PRIVATE)
#ifndef _PTH_AMIX_PUBLIC_TIMESPEC
#define _PTH_AMIX_PUBLIC_TIMESPEC
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
#endif
#endif
struct timespec;"""
        backup = header.with_name(header.name + ".amix-original")
        if not backup.exists():
            backup.write_text(public)
        header.write_text(public.replace(anchor, fallback))
print("GNU Pth AMIX public timespec fallback ready")

# Upstream's pthread_once sets the done flag after running the initializer.
# If that initializer yields, peers can run it again. Serialize through a
# per-control in-progress state; use Pth's own yield and cleanup mechanisms.
path = root / "pthread.c"
text = path.read_text()
marker = "pthread_once_amix_reset"
if marker not in text:
    old = """int pthread_once(
    pthread_once_t *once_control, void (*init_routine)(void))
{
    pthread_initialize();
    if (once_control == NULL || init_routine == NULL)
        return pth_error(EINVAL, EINVAL);
    if (*once_control != 1)
        init_routine();
    *once_control = 1;
    return OK;
}"""
    new = """#if defined(__amix__)
static void pthread_once_amix_reset(void *argument)
{
    *(pthread_once_t *)argument = PTHREAD_ONCE_INIT;
}
#endif

int pthread_once(
    pthread_once_t *once_control, void (*init_routine)(void))
{
    pthread_initialize();
    if (once_control == NULL || init_routine == NULL)
        return pth_error(EINVAL, EINVAL);
#if defined(__amix__)
    /* No preemption: checking and setting state needs no kernel atomic. */
    while (*once_control != 1) {
        if (*once_control == PTHREAD_ONCE_INIT) {
            *once_control = 2;
            if (!pth_cleanup_push(pthread_once_amix_reset, once_control)) {
                *once_control = PTHREAD_ONCE_INIT;
                return errno;
            }
            init_routine();
            *once_control = 1;
            pth_cleanup_pop(FALSE);
        }
        else
            pth_yield(NULL);
    }
#else
    if (*once_control != 1)
        init_routine();
    *once_control = 1;
#endif
    return OK;
}"""
    if text.count(old) != 1:
        raise SystemExit("Unexpected GNU Pth pthread_once source")
    backup = path.with_name(path.name + ".amix-original")
    if not backup.exists():
        backup.write_text(text)
    path.write_text(text.replace(old, new))
print("GNU Pth AMIX yielding pthread_once fix ready")
