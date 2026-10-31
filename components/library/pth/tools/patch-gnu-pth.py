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

# With the sjlj machine context (PTH_MCTX in build-gnu-pth.sh), configure
# picks _setjmp/_longjmp, which AMIX's libc does not have, and AMIX's setjmp
# saves d2-d7/a2-a7 only.  gcc keeps fp2-fp7 across calls too, and Pth's mcsc
# path preserved them and the FPCR (rounding mode), so a switch must as
# well.  pth_amix_setjmp/pth_amix_longjmp: AMIX's setjmp plus fp2-fp7 and
# FPCR, no system call -- the reason for using sjlj on AMIX at all.
acmac = root / "pth_acmac.h"
text = acmac.read_text()
marker = "pth_amix_setjmp"
if "#define pth_sigsetjmp(buf) _setjmp(buf)" in text and marker not in text:
    old = """#define pth_sigjmpbuf jmp_buf
#define pth_sigsetjmp(buf) _setjmp(buf)
#define pth_siglongjmp(buf,val) _longjmp(buf,val)"""
    new = """#if defined(__amix__)
/* AMIX: see pth_mctx.c (d2-d7, a1-a7, fp2-fp7, fpcr) */
typedef long pth_amix_jmpbuf[33];
extern int pth_amix_setjmp(long *) __attribute__((returns_twice));
extern void pth_amix_longjmp(long *, int) __attribute__((noreturn));
#define pth_sigjmpbuf pth_amix_jmpbuf
#define pth_sigsetjmp(buf) pth_amix_setjmp(buf)
#define pth_siglongjmp(buf,val) pth_amix_longjmp(buf,val)
#else
#define pth_sigjmpbuf jmp_buf
#define pth_sigsetjmp(buf) _setjmp(buf)
#define pth_siglongjmp(buf,val) _longjmp(buf,val)
#endif"""
    if text.count(old) != 1:
        raise SystemExit("Unexpected GNU Pth sjlj macros")
    backup = acmac.with_name(acmac.name + ".amix-original")
    if not backup.exists():
        backup.write_text(text)
    acmac.write_text(text.replace(old, new))
    path = root / "pth_mctx.c"
    text = path.read_text()
    if marker not in text:
        anchor = '#include "pth_p.h"\n'
        if text.count(anchor) != 1:
            raise SystemExit("Unexpected GNU Pth pth_mctx.c includes")
        asm = anchor + r'''
#if defined(__amix__) && PTH_MCTX_MTH(sjlj)
/*
 * AMIX: setjmp/longjmp for thread switches.  Like libc's setjmp (return
 * address popped, then d2-d7, a1 = return address, a2-a7 at 0..55) plus the
 * call-saved FPU registers fp2-fp7 (56..127) and the FPCR (128): 132 bytes,
 * pth_amix_jmpbuf.  No system call: the signal mask is Pth's business.
 */
__asm__(
"	.text\n"
"	.globl	pth_amix_setjmp\n"
"pth_amix_setjmp:\n"
"	movel	%sp@+,%a1\n"
"	movel	%sp@,%a0\n"
"	moveml	%d2-%d7/%a1-%sp,%a0@\n"
"	fmovemx	%fp2-%fp7,%a0@(56)\n"
"	fmovel	%fpcr,%a0@(128)\n"
"	clrl	%d0\n"
"	jmp	%a1@\n"
"	.globl	pth_amix_longjmp\n"
"pth_amix_longjmp:\n"
"	addql	#4,%sp\n"
"	movel	%sp@+,%a0\n"
"	movel	%sp@,%d0\n"
"	bnes	1f\n"
"	moveq	#1,%d0\n"
"1:	fmovemx	%a0@(56),%fp2-%fp7\n"
"	fmovel	%a0@(128),%fpcr\n"
"	moveml	%a0@,%d2-%d7/%a1-%sp\n"
"	jmp	%a1@\n");
#endif
'''
        path.write_text(text.replace(anchor, asm))
    print("GNU Pth AMIX sjlj switch (with fp2-fp7, fpcr) ready")

# A new thread under sjlj: Pth's signal-stack trick (sigaltstack, a signal to
# itself, sigsuspend) takes a SIGSEGV in AMIX's signal delivery.  Like the
# Linux, ISC and Win32 variants, set the stack pointer and start address in
# the saved context instead: pth_amix_jmpbuf word 6 is a1, the address the
# longjmp jumps to, word 12 the stack pointer.
path = root / "pth_mctx.c"
text = path.read_text()
marker = "pth_amix_mctx_set"
if "pth_amix_setjmp" in text and marker not in text:
    anchor = """#elif PTH_MCTX_MTH(sjlj)     &&\\
      !PTH_MCTX_DSP(sjljlx)  &&\\"""
    if text.count(anchor) != 1:
        raise SystemExit("Unexpected GNU Pth sjlj variants")
    variant = """#elif PTH_MCTX_MTH(sjlj) && defined(__amix__)

/*
 * AMIX: the start address and stack written into the pth_amix_jmpbuf
 * (pth_amix_mctx_set).  The function never returns (pth_spawn_trampoline
 * ends in pth_exit); it gets a zero return address on a 16-byte aligned
 * stack top.  The FPU state saved here is the creator's, as getcontext's was.
 */
intern int pth_mctx_set(
    pth_mctx_t *mctx, void (*func)(void), char *sk_addr_lo, char *sk_addr_hi)
{
    unsigned long sp = ((unsigned long)sk_addr_hi & ~15UL) - 16;

    (void)sk_addr_lo;
    if (pth_mctx_save(mctx) != 0)
        abort();                        /* only ever entered at func */
    *(unsigned long *)sp = 0;           /* func's return address */
    mctx->jb[6] = (long)func;
    mctx->jb[12] = (long)sp;
    sigemptyset(&mctx->sigs);
    mctx->error = 0;
    return TRUE;
}

"""
    path.write_text(text.replace(anchor, variant + anchor.replace("#elif PTH_MCTX_MTH(sjlj)     &&", "#elif PTH_MCTX_MTH(sjlj)     && !defined(__amix__) &&", 1)))
    print("GNU Pth AMIX sjlj thread start (pth_amix_mctx_set) ready")
