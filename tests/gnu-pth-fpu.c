/*
 * gnu-pth-fpu.c -- the FPU state survives thread switches.
 *
 * Four threads each keep six float values live across pth_yield() (gcc puts
 * them in the call-saved fp2-fp7) and run in their own FPCR rounding mode.
 * The results must equal the same computation done without threads.  With a
 * switch that saves only the integer registers (AMIX libc's setjmp) the
 * values of one thread end up in another, and so do the rounding modes.
 */
#include <pthread.h>
#include <stdio.h>

#define THREADS 4
#define ROUNDS 2000

static double result[THREADS];
static unsigned long mode_bad[THREADS];

static unsigned long get_fpcr(void)
{
    unsigned long v;

    __asm__ volatile("fmove.l %%fpcr,%0" : "=d"(v));
    return v;
}

static void set_fpcr(unsigned long v)
{
    __asm__ volatile("fmove.l %0,%%fpcr" : : "d"(v));
}

static double work(int id, int yield)
{
    double a = 1.0 + id, b = 0.5 * id, c = 3.25, d = -1.0 * id, e = 0.125, f = 7.0 + id;
    unsigned long mode = (unsigned long)(id & 3) << 4;   /* RN, RZ, RM, RP */
    int i;

    set_fpcr(mode);
    for (i = 0; i < ROUNDS; i++) {
        a = a * 1.0001 + b;
        b = b * 0.9999 - c / 1000.0;
        c = c + d * 0.001;
        d = d - e * 0.0001;
        e = e * 1.00001 + 0.000001;
        f = f / 1.0001 + a * 0.00001;
        if (yield) {
            pthread_yield_np();
            if (get_fpcr() != mode)
                mode_bad[id]++;
        }
    }
    set_fpcr(0);
    return a + b + c + d + e + f;
}

static void *thread(void *arg)
{
    int id = (int)(long)arg;

    result[id] = work(id, 1);
    return NULL;
}

int main(void)
{
    pthread_t t[THREADS];
    int i, bad = 0;

    for (i = 0; i < THREADS; i++)
        pthread_create(&t[i], NULL, thread, (void *)(long)i);
    for (i = 0; i < THREADS; i++)
        pthread_join(t[i], NULL);
    for (i = 0; i < THREADS; i++) {
        double want = work(i, 0);

        if (result[i] != want || mode_bad[i]) {
            printf("thread %d: %.17g, expected %.17g, rounding mode lost %lu times\n",
                   i, result[i], want, mode_bad[i]);
            bad = 1;
        }
    }
    printf(bad ? "FAIL: FPU state not preserved across switches\n"
               : "PASS: fp registers and FPCR preserved across %d switches\n",
           THREADS * ROUNDS);
    return bad;
}
