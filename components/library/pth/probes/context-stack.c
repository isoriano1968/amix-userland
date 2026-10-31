/* Separate-stack context probe. Link failure is a useful capability result.
 * No scheduler, threads, or signal preemption. Run only in a probe directory.
 */
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <ucontext.h>

static ucontext_t caller;
static ucontext_t worker;
static union {
    double alignment;
    char bytes[32768];
} worker_stack;
static volatile sig_atomic_t visited;

static void entry(void)
{
    volatile char stack_marker;
    unsigned long address = (unsigned long)&stack_marker;
    unsigned long base = (unsigned long)worker_stack.bytes;
    if (address < base || address >= base + sizeof(worker_stack.bytes)) {
        visited = -1;
    } else {
        visited = 1;
    }
    if (swapcontext(&worker, &caller) == -1)
        _exit(4);
    _exit(5);
}

int main(void)
{
    alarm(5);
    if (getcontext(&worker) == -1) {
        perror("getcontext");
        return 1;
    }
    worker.uc_stack.ss_sp = worker_stack.bytes;
    worker.uc_stack.ss_size = sizeof(worker_stack.bytes);
    worker.uc_stack.ss_flags = 0;
    worker.uc_link = &caller;
    makecontext(&worker, entry, 0);
    if (swapcontext(&caller, &worker) == -1) {
        perror("swapcontext");
        return 2;
    }
    alarm(0);
    if (visited != 1) {
        puts("FAIL: worker did not execute on its supplied stack");
        return 3;
    }
    puts("PASS: separate stack and swapcontext roundtrip");
    puts("Not tested: ABI/FPU preservation, signal masks, scheduler, uc_link");
    return 0;
}
