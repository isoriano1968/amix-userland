/* test060.c -- lib060 on the box: libm060 against reference values,
   libc060 against byte loops (all alignments, sizes 0..300), and timings */
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
extern int gettimeofday();
double sin(double), cos(double), tan(double), fmod(double, double);
static double now(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec + t.tv_usec / 1e6; }

static const struct { double x, s, c; } ref[] = {     /* glibc on the PC */
    { 0.0, 0.0, 1.0 },
    { 0.5, 0.479425538604203, 0.877582561890373 },
    { 1.0, 0.841470984807897, 0.540302305868140 },
    { 2.5, 0.598472144103957, -0.801143615546934 },
    { -3.7, 0.529836140908493, -0.848100031710408 },
    { 10.0, -0.544021110889370, -0.839071529076452 },
    { 100.0, -0.506365641109759, 0.862318872287684 },
    { 12345.678, -0.704081313753382, 0.710119358716063 },
};

static unsigned char a[1024], b[1024], r[1024];

static int check_mem(void)
{
    int sa, da, n, i, bad = 0;

    for (sa = 0; sa < 4; sa++)
        for (da = 0; da < 4; da++)
            for (n = 0; n <= 300; n++) {
                for (i = 0; i < 1024; i++) { a[i] = (unsigned char) (i * 7 + 3); b[i] = r[i] = 0x55; }
                memcpy(b + 64 + da, a + 64 + sa, n);
                for (i = 0; i < n; i++) r[64 + da + i] = a[64 + sa + i];
                if (memcmp(b, r, 1024) != 0) { bad++; continue; }
                /* memmove both ways, overlapping */
                for (i = 0; i < 1024; i++) b[i] = r[i] = (unsigned char) (i * 13);
                memmove(b + 100 + da, b + 100 + sa + 5, n);
                for (i = 0; i < n; i++) r[100 + da + i] = r[100 + sa + 5 + i];
                if (memcmp(b, r, 1024) != 0) { bad++; continue; }
                for (i = 0; i < 1024; i++) b[i] = r[i] = (unsigned char) (i * 13);
                memmove(b + 100 + da + 5, b + 100 + sa, n);
                for (i = n - 1; i >= 0; i--) r[100 + da + 5 + i] = r[100 + sa + i];
                if (memcmp(b, r, 1024) != 0) { bad++; continue; }
                for (i = 0; i < 1024; i++) b[i] = r[i] = 0x55;
                memset(b + 64 + da, 0xa7, n);
                for (i = 0; i < n; i++) r[64 + da + i] = 0xa7;
                if (memcmp(b, r, 1024) != 0) { bad++; continue; }
            }
    /* memcmp's sign */
    if (memcmp("abcd1", "abcd2", 5) >= 0 || memcmp("abce", "abcd", 4) <= 0 || memcmp("xy", "xy", 2))
        bad++;
    return bad;
}

int main(void)
{
    static unsigned char big1[65536], big2[65536];
    volatile double acc = 0;
    double x, t, err = 0, e;
    int i, n = 200000;

    for (i = 0; i < (int) (sizeof ref / sizeof ref[0]); i++) {
        e = sin(ref[i].x) - ref[i].s; if (e < 0) e = -e; if (e > err) err = e;
        e = cos(ref[i].x) - ref[i].c; if (e < 0) e = -e; if (e > err) err = e;
    }
    printf("max error sin/cos: %.3g\n", err);
    printf("tan(1) %.15f (1.557407724654902), fmod(10.5,3) %.15f (1.5), fmod(-7.25,2) %.15f (-1.25)\n",
           tan(1.0), fmod(10.5, 3.0), fmod(-7.25, 2.0));
    t = now(); for (i = 0, x = 0; i < n; i++, x += 0.001) acc += sin(x); t = now() - t;
    printf("sin: %.3f us\n", t * 1e6 / n);
    t = now(); for (i = 0, x = 0; i < n; i++, x += 0.001) acc += fmod(x, 4.0); t = now() - t;
    printf("fmod: %.3f us\n", t * 1e6 / n);
    printf("mem* errors: %d\n", check_mem());
    t = now(); for (i = 0; i < 400; i++) memcpy(big2, big1 + (i & 1), 65536 - 2); t = now() - t;
    printf("memcpy 64 KB: %.1f MB/s\n", 400 * 65534 / t / 1e6);
    t = now(); for (i = 0; i < 400; i++) memset(big2, i, 65536); t = now() - t;
    printf("memset 64 KB: %.1f MB/s\n", 400 * 65536 / t / 1e6);
    t = now(); for (i = 0; i < 200000; i++) memcpy(big2 + 4, big1 + 8, 152); t = now() - t;
    printf("memcpy 152 bytes (a GLVertex): %.2f us\n", t * 1e6 / 200000);
    return 0;
}
