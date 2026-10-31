/*
 * crashpc.c -- link it into an AMIX program to learn where it dies
 * (2026-10-27): on SIGILL, SIGSEGV, SIGBUS and SIGFPE it prints the signal,
 * the program counter (from the ucontext), the instruction words there and
 * the fault address, then exits.  Map the PC with nm -n.
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <siginfo.h>
#include <ucontext.h>
#include <unistd.h>

static void crash(int sig, siginfo_t *si, ucontext_t *uc)
{
    unsigned long pc = (unsigned long) uc->uc_mcontext.gregs[R_PC];
    unsigned short *w = (unsigned short *) pc;

    unsigned long *sp = (unsigned long *) uc->uc_mcontext.gregs[R_SP];

    fprintf(stderr, "crashpc: signal %d code %d at pc 0x%08lx addr 0x%08lx, words %04x %04x %04x\n",
            sig, si ? si->si_code : -1, pc, si ? (unsigned long) si->si_addr : 0UL,
            w[0], w[1], w[2]);
    /* a call through a null pointer lands at 0: the return address is on top */
    fprintf(stderr, "crashpc: sp 0x%08lx: %08lx %08lx %08lx %08lx\n",
            (unsigned long) sp, sp[0], sp[1], sp[2], sp[3]);
    _exit(128 + sig);
}

static void crashpc_init(void) __attribute__((constructor));
static void crashpc_init(void)
{
    struct sigaction sa;
    int s[] = { SIGILL, SIGSEGV, SIGBUS, SIGFPE };
    int i;

    sa.sa_handler = (void (*)()) crash;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    for (i = 0; i < 4; i++)
        sigaction(s[i], &sa, (struct sigaction *) 0);
}
