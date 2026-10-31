/*
 * pcprof.c -- a PC-sampling profile of an AMIX program (2026-10-26).
 * Link pcprof.o into the program; with PCPROF=1 in the environment a
 * SIGPROF timer samples, every 1/60 s of the process's CPU time, the
 * interrupted program counter (from the signal's ucontext), and at exit
 * the counts go to /var/tmp/pcprof.out:
 *   "pcprof <lowpc> <shift> <n> <outside>" then one count per line
 * (counter i covers lowpc + (i << shift)); <outside> counts samples
 * elsewhere -- shared libraries, and system calls, whose time is charged
 * to the user PC they return to.  pcprof.py turns it into per-function
 * percentages with nm.  (profil(2) is no use here: on this kernel it
 * corrupts the program.)
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <siginfo.h>
#include <ucontext.h>
#include <sys/time.h>
#include <dlfcn.h>

extern int setitimer();
extern char _etext[];

#define LOWPC 0x80000000UL              /* AMIX ELF executables' text */
#define SHIFT 2                         /* one counter per 4 bytes */

static unsigned long *cnt, ncnt, outside;

/* outside the program: counts per 256-byte block, open hashing */
#define NOUT 1024
static unsigned long out_pc[NOUT], out_n[NOUT];

static void tick(int sig, siginfo_t *si, ucontext_t *uc)
{
    unsigned long pc = (unsigned long) uc->uc_mcontext.gregs[R_PC];

    if (pc >= LOWPC && ((pc - LOWPC) >> SHIFT) < ncnt)
        cnt[(pc - LOWPC) >> SHIFT]++;
    else {
        unsigned long blk = pc & ~0xffUL, h = (blk >> 8) & (NOUT - 1), k;

        outside++;
        for (k = 0; k < NOUT; k++, h = (h + 1) & (NOUT - 1))
            if (out_pc[h] == blk || out_n[h] == 0) {
                out_pc[h] = blk;
                out_n[h]++;
                break;
            }
    }
}

static void pcprof_write(void)
{
    struct itimerval it;
    FILE *f;
    unsigned long i;

    it.it_interval.tv_sec = it.it_interval.tv_usec = 0;
    it.it_value = it.it_interval;
    setitimer(ITIMER_PROF, &it, (struct itimerval *) 0);
    if ((f = fopen("/var/tmp/pcprof.out", "w")) == NULL)
        return;
    fprintf(f, "pcprof %lx %d %lu %lu\n", LOWPC, SHIFT, ncnt, outside);
    for (i = 0; i < ncnt; i++)
        fprintf(f, "%lu\n", cnt[i]);
    {
        /* where libc.so.1 is: one of its symbols' address (pcprof.py
           subtracts the symbol's value in the file) */
        void *h = dlopen("/usr/lib/libc.so.1", RTLD_LAZY);
        void *sym = h ? dlsym(h, "fprintf") : 0;
        fprintf(f, "libc fprintf %lx\n", (unsigned long) sym);
    }
    for (i = 0; i < NOUT; i++)
        if (out_n[i])
            fprintf(f, "o %lx %lu\n", out_pc[i], out_n[i]);
    fclose(f);
}

static void pcprof_start(void) __attribute__((constructor));
static void pcprof_start(void)
{
    struct sigaction sa;
    struct itimerval it;

    if (!getenv("PCPROF"))
        return;
    ncnt = (((unsigned long) _etext - LOWPC) >> SHIFT) + 1;
    if ((cnt = (unsigned long *) calloc(ncnt, sizeof *cnt)) == NULL)
        return;
    sa.sa_handler = (void (*)()) tick;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGPROF, &sa, (struct sigaction *) 0);
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 16667;
    it.it_value = it.it_interval;
    setitimer(ITIMER_PROF, &it, (struct itimerval *) 0);
    atexit(pcprof_write);
}
