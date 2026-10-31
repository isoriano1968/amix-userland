/* GCC/m68k context-state stress test. Does not enable preemption. */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <ucontext.h>

#define ROUNDS 2000
static ucontext_t main_context, context[2];
static union { double align; char bytes[32768]; } stacks[2];
static volatile int done[2], iterations[2], failures;
static volatile int errno_shared;

static void run(int id)
{
    int i, blocked;
    sigset_t mask;
    volatile unsigned long local = 0x12340000UL + id;
    unsigned long original_fpcr, requested_fpcr, actual_fpcr;
    register unsigned long integer asm("d2") = 0x45670000UL + id;
    register double floating asm("fp2") = id ? 2345.25 : 1234.5;

    asm volatile("fmove.l %%fpcr,%0" : "=d"(original_fpcr));
    requested_fpcr = (original_fpcr & ~0x30UL) | (id ? 0x20UL : 0x10UL);
    asm volatile("fmove.l %0,%%fpcr" : : "d"(requested_fpcr));
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    if (sigprocmask(id ? SIG_UNBLOCK : SIG_BLOCK, &mask, 0) == -1)
        _exit(10);

    for (i = 0; i < ROUNDS; ++i) {
        errno = 100 + id;
        asm volatile("" : "=d"(integer), "=f"(floating)
                     : "0"(integer), "1"(floating));
        if (swapcontext(&context[id], &context[1-id]) == -1)
            _exit(11);
        asm volatile("" : "=d"(integer), "=f"(floating)
                     : "0"(integer), "1"(floating));
        if (integer != 0x45670000UL + id ||
            floating != (id ? 2345.25 : 1234.5) ||
            local != 0x12340000UL + id)
            ++failures;
        asm volatile("fmove.l %%fpcr,%0" : "=d"(actual_fpcr));
        if (actual_fpcr != requested_fpcr)
            ++failures;
        if (errno != 100 + id)
            errno_shared = 1;
        if (sigprocmask(SIG_BLOCK, 0, &mask) == -1)
            _exit(12);
        blocked = sigismember(&mask, SIGUSR1);
        if (blocked != !id)
            ++failures;
        ++iterations[id];
    }
    asm volatile("fmove.l %0,%%fpcr" : : "d"(original_fpcr));
    done[id] = 1;
    if (setcontext(done[1-id] ? &main_context : &context[1-id]) == -1)
        _exit(13);
    _exit(14);
}

static void first(void) { run(0); }
static void second(void) { run(1); }

int main(void)
{
    int i;
    alarm(30);
    for (i = 0; i < 2; ++i) {
        if (getcontext(&context[i]) == -1)
            return 1;
        context[i].uc_stack.ss_sp = stacks[i].bytes;
        context[i].uc_stack.ss_size = sizeof(stacks[i].bytes);
        context[i].uc_stack.ss_flags = 0;
        context[i].uc_link = &main_context;
        makecontext(&context[i], i ? second : first, 0);
    }
    if (swapcontext(&main_context, &context[0]) == -1)
        return 2;
    alarm(0);
    printf("iterations=%d,%d state-failures=%d errno-shared=%d\n",
           iterations[0], iterations[1], failures, errno_shared);
    if (iterations[0] != ROUNDS || iterations[1] != ROUNDS || failures)
        return 3;
    puts("PASS: stack locals, d2, fp2, FPCR, signal masks across switches");
    puts("Scope: cooperative switching; selected registers only");
    return 0;
}
