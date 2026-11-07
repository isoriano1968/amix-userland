/* test060.c -- lib060 on the box: libm060 against reference values,
   libc060 against byte loops (all alignments, sizes 0..300), and timings.
   The musl members (atan ... y1) against test060-expected.h, from the PC
   (`make expected`): specials bit for bit as glibc, the others bit for bit
   as the same code on the PC and their error in ulps against MPFR. */
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
extern int gettimeofday();
double sin(double), cos(double), tan(double), fmod(double, double);
double atan(double), atan2(double, double), hypot(double, double), pow(double, double);
float atanf(float), atan2f(float, float);
double erf(double), erfc(double), lgamma(double), gamma(double);
double j0(double), j1(double), y0(double), y1(double), jn(int, double), yn(int, double);
extern int signgam;
#include "test060-expected.h"
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

static const char *fname[] = { "atan", "atan2", "atanf", "atan2f", "hypot", "pow", "erf",
    "erfc", "lgamma", "j0", "j1", "y0", "y1", "jn", "yn" };
#define NF 15

union dw { double d; unsigned long w[2]; };
union fw { float f; unsigned long w; };

static double call(int f, double x, double y)
{
    switch (f) {
    case 0:  return atan(x);
    case 1:  return atan2(x, y);
    case 2:  return atanf((float) x);
    case 3:  return atan2f((float) x, (float) y);
    case 4:  return hypot(x, y);
    case 5:  return pow(x, y);
    case 6:  return erf(x);
    case 7:  return erfc(x);
    case 8:  return lgamma(x);
    case 9:  return j0(x);
    case 10: return j1(x);
    case 11: return y0(x);
    case 12: return y1(x);
    case 13: return jn((int) x, y);       /* x is the order */
    default: return yn((int) x, y);
    }
}

static int isnan_d(union dw *u) { return (u->w[0] & 0x7ff00000) == 0x7ff00000 && ((u->w[0] & 0xfffff) | u->w[1]); }
static int isnan_f(union fw *u) { return (u->w & 0x7f800000) == 0x7f800000 && (u->w & 0x7fffff); }

/* |a - b| in ulps of b (finite b) */
static double ulpdiff(double a, double b, int fl)
{
    double u, d = a - b, ab = b < 0 ? -b : b;
    int e = 0;

    if (d < 0) d = -d;
    if (ab == 0) return d == 0 ? 0 : 1e9;
    for (u = 1.0; u * 2 <= ab; u *= 2) e++;
    for (; u > ab; u /= 2) e--;
    u = 1.0;                                  /* 2^(e - p + 1), at least the denormal step */
    e -= fl ? 23 : 52;
    if (e < (fl ? -149 : -1074)) e = fl ? -149 : -1074;
    for (; e > 0; e--) u *= 2;
    for (; e < 0; e++) u /= 2;
    return d / u;
}

static int check_musl(void)
{
    double maxu[NF], x, y, r;
    int nbits[NF], ncase[NF], i, f, bad = 0;

    for (f = 0; f < NF; f++) maxu[f] = 0, nbits[f] = ncase[f] = 0;
    for (i = 0; i < (int) (sizeof tcases / sizeof tcases[0]); i++) {
        const struct tcase *c = &tcases[i];
        int fl = c->f == 2 || c->f == 3, ok;
        union dw ux, uy, ur, ue, ug;
        union fw fx, fy, fr, fe, fg;

        f = c->f;
        if (fl) {
            fx.w = c->x[0]; fy.w = c->y[0]; x = fx.f; y = fy.f;
        } else {
            ux.w[0] = c->x[0]; ux.w[1] = c->x[1]; uy.w[0] = c->y[0]; uy.w[1] = c->y[1];
            x = ux.d; y = uy.d;
        }
        signgam = 0;
        r = call(f, x, y);
        ncase[f]++;
        if (fl) {
            fr.f = (float) r; fe.w = c->r[0]; fg.w = c->ref[0];
            ok = fr.w == fe.w || (isnan_f(&fr) && isnan_f(&fe));
            if (c->special)
                ok = ok && (fr.w == fg.w || (isnan_f(&fr) && isnan_f(&fg)) ||
                            (fg.f != 0 && fg.f - fg.f == 0 && ulpdiff(fr.f, fg.f, 1) <= 1));
            else if (!isnan_f(&fr) && fg.f == fg.f && fg.f - fg.f == 0) {
                double u = ulpdiff(fr.f, fg.f, 1);
                if (u > maxu[f]) maxu[f] = u;
            }
            if (!ok) printf("%s(%08lx, %08lx) = %08lx, PC %08lx, reference %08lx\n",
                            fname[f], fx.w, fy.w, fr.w, fe.w, fg.w);
        } else {
            ur.d = r; ue.w[0] = c->r[0]; ue.w[1] = c->r[1]; ug.w[0] = c->ref[0]; ug.w[1] = c->ref[1];
            ok = (ur.w[0] == ue.w[0] && ur.w[1] == ue.w[1]) || (isnan_d(&ur) && isnan_d(&ue));
            if (c->special)          /* glibc's, or within 1 ulp of a finite non-zero one */
                ok = ok && ((ur.w[0] == ug.w[0] && ur.w[1] == ug.w[1]) || (isnan_d(&ur) && isnan_d(&ug)) ||
                            (ug.d != 0 && ug.d - ug.d == 0 && ulpdiff(ur.d, ug.d, 0) <= 1));
            else if (!isnan_d(&ur) && ug.d - ug.d == 0) {
                double u = ulpdiff(ur.d, ug.d, 0);
                if (u > maxu[f]) maxu[f] = u;
            }
            if (f == 8 && signgam != c->sg && !(c->special && c->sg == 0))
                ok = 0;
            if (!ok) printf("%s(%08lx%08lx, %08lx%08lx) = %08lx%08lx signgam %d, PC %08lx%08lx, reference %08lx%08lx signgam %d\n",
                            fname[f], ux.w[0], ux.w[1], uy.w[0], uy.w[1], ur.w[0], ur.w[1], signgam,
                            ue.w[0], ue.w[1], ug.w[0], ug.w[1], c->sg);
        }
        if (!ok) { nbits[f]++; bad++; }
    }
    for (f = 0; f < NF; f++)
        printf("%-7s %3d cases, %d not as on the PC, max %.2f ulp against MPFR\n",
               fname[f], ncase[f], nbits[f], maxu[f]);
    if (gamma(-2.5) != lgamma(-2.5)) { printf("gamma != lgamma\n"); bad++; }
    return bad;
}

#define TIME(name, expr) do { \
    t = now(); for (i = 0, x = 0.1; i < 20000; i++, x += 0.00004) acc += (expr); t = now() - t; \
    printf("%-7s %6.2f us\n", name, t * 1e6 / 20000); \
} while (0)

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
    printf("musl members: %d failed\n", check_musl());
    TIME("atan", atan(x));
    TIME("atan2", atan2(x, 0.7));
    TIME("atanf", atanf((float) x));
    TIME("atan2f", atan2f((float) x, 0.7f));
    TIME("hypot", hypot(x, 0.7));
    TIME("pow", pow(x + 1.0, 1.7));
    TIME("erf", erf(x));
    TIME("erfc", erfc(x));
    TIME("lgamma", lgamma(x + 1.0));
    TIME("j0", j0(x * 10));
    TIME("j1", j1(x * 10));
    TIME("y0", y0(x * 10));
    TIME("y1", y1(x * 10));
    TIME("jn(3)", jn(3, x * 10));
    printf("mem* errors: %d\n", check_mem());
    t = now(); for (i = 0; i < 400; i++) memcpy(big2, big1 + (i & 1), 65536 - 2); t = now() - t;
    printf("memcpy 64 KB: %.1f MB/s\n", 400 * 65534 / t / 1e6);
    t = now(); for (i = 0; i < 400; i++) memset(big2, i, 65536); t = now() - t;
    printf("memset 64 KB: %.1f MB/s\n", 400 * 65536 / t / 1e6);
    t = now(); for (i = 0; i < 200000; i++) memcpy(big2 + 4, big1 + 8, 152); t = now() - t;
    printf("memcpy 152 bytes (a GLVertex): %.2f us\n", t * 1e6 / 200000);
    return 0;
}
