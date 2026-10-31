/* Isolated libc context probe; no threading or custom stacks yet.
 * Native GCC 2.7 and GCC 14 builds should be recorded separately.
 * The alarm bounds a broken restore path. A signal/crash is a failed probe.
 */
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <ucontext.h>

static ucontext_t saved;
static volatile sig_atomic_t restored;

int main(void)
{
    alarm(5);
    printf("context-size=%lu\n", (unsigned long)sizeof(saved));
    fflush(stdout);
    if (getcontext(&saved) == -1) {
        perror("getcontext");
        return 1;
    }
    if (!restored) {
        restored = 1;
        if (setcontext(&saved) == -1) {
            perror("setcontext");
            return 2;
        }
        fputs("FAIL: setcontext returned\n", stderr);
        return 3;
    }
    alarm(0);
    puts("PASS: getcontext/setcontext control-flow roundtrip");
    puts("Not tested: new stacks, swapcontext, signal masks, registers, FPU");
    return 0;
}
