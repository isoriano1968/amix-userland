/*
 * native060.c -- lib060's musl members, built for the PC, against MPFR and
 * glibc.
 *
 * `make native-test` builds the same musl groups as libm060.a for x86-64,
 * renamed m060_<name>.  For every function this program checks the special
 * values (inf, nan, signed zeros, atan2's quadrants, pow's C99 Annex F
 * cases, lgamma's poles and signgam) bit for bit against glibc, and NRAND
 * random inputs per range against MPFR's correctly rounded result: the
 * largest error in ulps, for lib060 and for glibc.
 *
 *   native060 [-n NRAND] [-g FILE]
 *
 * -g writes FILE (test060-expected.h): the specials and a few random inputs
 * per range, each with the PC's result (the same algorithm in double on the
 * 68060 should give the same bits) and the reference (glibc's for a special,
 * MPFR's correctly rounded value otherwise).
 *
 * MPFR comes from the PC's libmpfr.so.6; it has no headers installed, so the
 * few declarations it needs are here (MPFR 4 ABI).
 */
#define _GNU_SOURCE
#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct { long prec; int sign; long exp; void *d; } mpfr_s;
typedef mpfr_s mpfr_t[1];
enum { RNDN = 0 };
void mpfr_init2(mpfr_t, long);
void mpfr_clear(mpfr_t);
int mpfr_set_d(mpfr_t, double, int);
double mpfr_get_d(const mpfr_t, int);
int mpfr_sub(mpfr_t, const mpfr_t, const mpfr_t, int);
int mpfr_atan(mpfr_t, const mpfr_t, int);
int mpfr_atan2(mpfr_t, const mpfr_t, const mpfr_t, int);
int mpfr_hypot(mpfr_t, const mpfr_t, const mpfr_t, int);
int mpfr_pow(mpfr_t, const mpfr_t, const mpfr_t, int);
int mpfr_erf(mpfr_t, const mpfr_t, int);
int mpfr_erfc(mpfr_t, const mpfr_t, int);
int mpfr_lgamma(mpfr_t, int *, const mpfr_t, int);
int mpfr_j0(mpfr_t, const mpfr_t, int);
int mpfr_j1(mpfr_t, const mpfr_t, int);
int mpfr_y0(mpfr_t, const mpfr_t, int);
int mpfr_y1(mpfr_t, const mpfr_t, int);
int mpfr_jn(mpfr_t, long, const mpfr_t, int);
int mpfr_yn(mpfr_t, long, const mpfr_t, int);

/* lib060's, renamed */
double m060_atan(double), m060_atan2(double, double), m060_hypot(double, double);
float m060_atanf(float), m060_atan2f(float, float);
double m060_pow(double, double), m060_erf(double), m060_erfc(double);
double m060_lgamma(double), m060_gamma(double), m060__lgamma(double);
double m060_j0(double), m060_j1(double), m060_y0(double), m060_y1(double);
double m060__j0(double), m060__j1(double), m060__y0(double), m060__y1(double);
double m060_jn(int, double), m060_yn(int, double), m060__jn(int, double), m060__yn(int, double);
double m060__erf(double), m060__erfc(double);
extern int m060_signgam;

enum { F_ATAN, F_ATAN2, F_ATANF, F_ATAN2F, F_HYPOT, F_POW, F_ERF, F_ERFC, F_LGAMMA,
       F_J0, F_J1, F_Y0, F_Y1, F_JN, F_YN, NF };
static const char *fname[NF] = { "atan", "atan2", "atanf", "atan2f", "hypot", "pow", "erf",
	"erfc", "lgamma", "j0", "j1", "y0", "y1", "jn", "yn" };
/* jn and yn: x is the order n, y the argument */
static const int arity[NF] = { 1, 2, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 2, 2 };
static const int isfloat[NF] = { 0, 0, 1, 1 };

static double call(int f, double x, double y, int lib, int *sg)
{
	if (lib) {
		switch (f) {
		case F_ATAN: return m060_atan(x);
		case F_ATAN2: return m060_atan2(x, y);
		case F_ATANF: return m060_atanf((float) x);
		case F_ATAN2F: return m060_atan2f((float) x, (float) y);
		case F_HYPOT: return m060_hypot(x, y);
		case F_POW: return m060_pow(x, y);
		case F_ERF: return m060_erf(x);
		case F_ERFC: return m060_erfc(x);
		case F_LGAMMA: { double r = m060_lgamma(x); *sg = m060_signgam; return r; }
		case F_J0: return m060_j0(x);
		case F_J1: return m060_j1(x);
		case F_Y0: return m060_y0(x);
		case F_Y1: return m060_y1(x);
		case F_JN: return m060_jn((int) x, y);
		case F_YN: return m060_yn((int) x, y);
		}
	} else {
		switch (f) {
		case F_ATAN: return atan(x);
		case F_ATAN2: return atan2(x, y);
		case F_ATANF: return atanf((float) x);
		case F_ATAN2F: return atan2f((float) x, (float) y);
		case F_HYPOT: return hypot(x, y);
		case F_POW: return pow(x, y);
		case F_ERF: return erf(x);
		case F_ERFC: return erfc(x);
		case F_LGAMMA: { double r = lgamma(x); *sg = signgam; return r; }
		case F_J0: return j0(x);
		case F_J1: return j1(x);
		case F_Y0: return y0(x);
		case F_Y1: return y1(x);
		case F_JN: return jn((int) x, y);
		case F_YN: return yn((int) x, y);
		}
	}
	return 0;
}

/* MPFR's value at 256 bits into e; its sign of gamma into *sg */
static void exact(int f, double x, double y, mpfr_t e, int *sg)
{
	mpfr_t mx, my;

	mpfr_init2(mx, 64); mpfr_init2(my, 64);
	mpfr_set_d(mx, x, RNDN); mpfr_set_d(my, y, RNDN);
	switch (f) {
	case F_ATAN: case F_ATANF: mpfr_atan(e, mx, RNDN); break;
	case F_ATAN2: case F_ATAN2F: mpfr_atan2(e, mx, my, RNDN); break;
	case F_HYPOT: mpfr_hypot(e, mx, my, RNDN); break;
	case F_POW: mpfr_pow(e, mx, my, RNDN); break;
	case F_ERF: mpfr_erf(e, mx, RNDN); break;
	case F_ERFC: mpfr_erfc(e, mx, RNDN); break;
	case F_LGAMMA: mpfr_lgamma(e, sg, mx, RNDN); break;
	case F_J0: mpfr_j0(e, mx, RNDN); break;
	case F_J1: mpfr_j1(e, mx, RNDN); break;
	case F_Y0: mpfr_y0(e, mx, RNDN); break;
	case F_Y1: mpfr_y1(e, mx, RNDN); break;
	case F_JN: mpfr_jn(e, (long) x, my, RNDN); break;
	case F_YN: mpfr_yn(e, (long) x, my, RNDN); break;
	}
	mpfr_clear(mx); mpfr_clear(my);
}

/* the error of r in ulps of the correctly rounded result (float or double) */
static double ulps(double r, mpfr_t e, int fl)
{
	double d = mpfr_get_d(e, RNDN), u, err;
	int ex;
	mpfr_t mr, diff;

	if (fl)
		d = (float) d;
	if (isnan(r) || isinf(r) || isinf(d) || isnan(d))
		return (r == d || (isnan(r) && isnan(d))) ? 0 : 1e9;
	frexp(d, &ex);
	if (fl)
		u = ldexp(1.0, (d == 0 || ex - 24 < -149) ? -149 : ex - 24);
	else
		u = ldexp(1.0, (d == 0 || ex - 53 < -1074) ? -1074 : ex - 53);
	mpfr_init2(mr, 64); mpfr_init2(diff, 256);
	mpfr_set_d(mr, r, RNDN);
	mpfr_sub(diff, mr, e, RNDN);
	err = fabs(mpfr_get_d(diff, RNDN)) / u;
	mpfr_clear(mr); mpfr_clear(diff);
	return err;
}

/* ---- inputs -------------------------------------------------------------- */
static uint64_t rs = 0x2545f4914f6cdd1dULL;
static double urand(void)
{
	rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
	return (rs >> 11) * 0x1p-53;
}
/* kinds of range: uniform in [a,b]; log-uniform |x| in [a,b], sign + or random */
enum { U, LP, LS };
struct range { int f, kx; double ax, bx; int ky; double ay, by; };
static double pick(int k, double a, double b)
{
	if (k == U)
		return a + (b - a) * urand();
	double r = exp(log(a) + (log(b) - log(a)) * urand());
	return (k == LS && urand() < 0.5) ? -r : r;
}
static const struct range ranges[] = {
	{ F_ATAN, U, -4, 4 }, { F_ATAN, LS, 1e-10, 1e10 },
	{ F_ATAN2, LS, 1e-5, 1e5, LS, 1e-5, 1e5 }, { F_ATAN2, U, -2, 2, U, -2, 2 },
	{ F_ATANF, U, -4, 4 }, { F_ATANF, LS, 1e-10, 1e10 },
	{ F_ATAN2F, LS, 1e-5, 1e5, LS, 1e-5, 1e5 },
	{ F_HYPOT, U, -10, 10, U, -10, 10 }, { F_HYPOT, LS, 1e-300, 1e300, LS, 1e-300, 1e300 },
	{ F_POW, LP, 1e-3, 1e3, U, -50, 50 }, { F_POW, U, 0.5, 2, U, -700, 700 },
	{ F_POW, LP, 1e-300, 1e300, U, -1, 1 }, { F_POW, U, 0.999, 1.001, LS, 1e3, 1e6 },
	{ F_ERF, U, -6, 6 }, { F_ERF, LS, 1e-10, 1 },
	{ F_ERFC, U, -6, 6 }, { F_ERFC, U, 6, 27 },
	{ F_LGAMMA, U, 0, 30 }, { F_LGAMMA, U, -20, 0 }, { F_LGAMMA, LP, 30, 1e300 },
	{ F_LGAMMA, LS, 1e-10, 1 },
	{ F_J0, U, -50, 50 }, { F_J0, LP, 1e-10, 1e6 },
	{ F_J1, U, -50, 50 }, { F_J1, LP, 1e-10, 1e6 },
	{ F_Y0, U, 0, 50 }, { F_Y0, LP, 1e-10, 1e6 },
	{ F_Y1, U, 0, 50 }, { F_Y1, LP, 1e-10, 1e6 },
	{ F_JN, U, -10, 10, U, -30, 30 }, { F_YN, U, -10, 10, U, 0, 30 },
};
#define NRANGES (int) (sizeof ranges / sizeof ranges[0])

/* special inputs: lib060 must give glibc's bits (any NaN for a NaN), or for a
   finite non-zero result be within 1 ulp of glibc's (lgamma(-2.5) ...).
   Not here: j1(-inf), +0 from musl, -0 from glibc (POSIX leaves the sign). */
static const double I = INFINITY, N = NAN;
struct spec { int f; double x, y; };
static const struct spec specs[] = {
	{ F_ATAN, 0.0 }, { F_ATAN, -0.0 }, { F_ATAN, I }, { F_ATAN, -I }, { F_ATAN, N },
	{ F_ATAN, 1.0 }, { F_ATAN, -1.0 }, { F_ATAN, 1e-310 }, { F_ATAN, 1e300 },
	{ F_ATAN2, 0.0, 0.0 }, { F_ATAN2, -0.0, 0.0 }, { F_ATAN2, 0.0, -0.0 }, { F_ATAN2, -0.0, -0.0 },
	{ F_ATAN2, 0.0, -1 }, { F_ATAN2, -0.0, -1 }, { F_ATAN2, 0.0, 1 }, { F_ATAN2, -0.0, 1 },
	{ F_ATAN2, -1, 0.0 }, { F_ATAN2, -1, -0.0 }, { F_ATAN2, 1, 0.0 }, { F_ATAN2, 1, -0.0 },
	{ F_ATAN2, 1, -I }, { F_ATAN2, -1, -I }, { F_ATAN2, 1, I }, { F_ATAN2, -1, I },
	{ F_ATAN2, I, 1 }, { F_ATAN2, -I, 1 }, { F_ATAN2, I, -I }, { F_ATAN2, -I, -I },
	{ F_ATAN2, I, I }, { F_ATAN2, -I, I }, { F_ATAN2, N, 1 }, { F_ATAN2, 1, N },
	{ F_ATAN2, 1, 1 }, { F_ATAN2, 1, -1 }, { F_ATAN2, -1, -1 }, { F_ATAN2, -1, 1 },
	{ F_ATAN2, 1e300, 1e-300 }, { F_ATAN2, 1e-300, 1e300 }, { F_ATAN2, 1e-300, -1e300 },
	{ F_ATANF, 0.0 }, { F_ATANF, -0.0 }, { F_ATANF, I }, { F_ATANF, -I }, { F_ATANF, N },
	{ F_ATANF, 1.0 },
	{ F_ATAN2F, 0.0, 0.0 }, { F_ATAN2F, -0.0, -0.0 }, { F_ATAN2F, 0.0, -1 }, { F_ATAN2F, -0.0, 1 },
	{ F_ATAN2F, -1, 0.0 }, { F_ATAN2F, 1, -I }, { F_ATAN2F, -1, -I }, { F_ATAN2F, -I, -I },
	{ F_ATAN2F, I, I }, { F_ATAN2F, N, 1 }, { F_ATAN2F, -1, -1 },
	{ F_HYPOT, I, N }, { F_HYPOT, N, -I }, { F_HYPOT, -I, 1 }, { F_HYPOT, N, 1 },
	{ F_HYPOT, -3, 0.0 }, { F_HYPOT, -0.0, -0.0 }, { F_HYPOT, 3, 4 },
	{ F_HYPOT, 1e308, 1e308 }, { F_HYPOT, 1e-320, 1e-320 }, { F_HYPOT, DBL_MAX, 1 },
	{ F_POW, 0.0, -3 }, { F_POW, -0.0, -3 }, { F_POW, 0.0, -I }, { F_POW, -0.0, -I },
	{ F_POW, 0.0, -2 }, { F_POW, -0.0, -2 }, { F_POW, -0.0, -2.5 }, { F_POW, 0.0, 3 },
	{ F_POW, -0.0, 3 }, { F_POW, -0.0, 2 }, { F_POW, -0.0, 0.5 }, { F_POW, -1, I },
	{ F_POW, -1, -I }, { F_POW, 1, N }, { F_POW, 1, I }, { F_POW, N, 0.0 }, { F_POW, N, -0.0 },
	{ F_POW, I, 0.0 }, { F_POW, -2, 0.5 }, { F_POW, 0.5, -I }, { F_POW, -0.5, -I },
	{ F_POW, 2, -I }, { F_POW, -2, -I }, { F_POW, 0.5, I }, { F_POW, 2, I }, { F_POW, -2, I },
	{ F_POW, -I, -3 }, { F_POW, -I, -2 }, { F_POW, -I, -0.5 }, { F_POW, -I, 3 },
	{ F_POW, -I, 2 }, { F_POW, -I, 0.5 }, { F_POW, I, -1 }, { F_POW, I, 1 }, { F_POW, N, 1 },
	{ F_POW, 1, 0.0 }, { F_POW, 2, N }, { F_POW, -2, 3 }, { F_POW, -2, -3 }, { F_POW, -2, 2 },
	{ F_POW, 2, -1074 }, { F_POW, 2, -1075 }, { F_POW, 2, 1023 }, { F_POW, 2, 1024 },
	{ F_POW, 10, 308 }, { F_POW, 10, 309 }, { F_POW, 10, -323 }, { F_POW, 10, -324 },
	{ F_POW, -10, 309 }, { F_POW, -10, -325 }, { F_POW, 4, 0.5 }, { F_POW, 1e-310, 0.5 },
	{ F_POW, -8, 1.0 / 3 }, { F_POW, 0.0, 0.0 },
	{ F_ERF, 0.0 }, { F_ERF, -0.0 }, { F_ERF, I }, { F_ERF, -I }, { F_ERF, N }, { F_ERF, 1e-310 },
	{ F_ERF, 6 }, { F_ERF, -6 },
	{ F_ERFC, 0.0 }, { F_ERFC, -0.0 }, { F_ERFC, I }, { F_ERFC, -I }, { F_ERFC, N },
	{ F_ERFC, 27 }, { F_ERFC, 30 }, { F_ERFC, -30 },
	{ F_LGAMMA, 1 }, { F_LGAMMA, 2 }, { F_LGAMMA, 0.0 }, { F_LGAMMA, -0.0 }, { F_LGAMMA, -1 },
	{ F_LGAMMA, -2 }, { F_LGAMMA, -I }, { F_LGAMMA, I }, { F_LGAMMA, N }, { F_LGAMMA, -0.5 },
	{ F_LGAMMA, -1.5 }, { F_LGAMMA, -3.5 }, { F_LGAMMA, 0.5 }, { F_LGAMMA, 1e-310 },
	{ F_LGAMMA, -1e-310 }, { F_LGAMMA, 1e306 }, { F_LGAMMA, 2.6e305 }, { F_LGAMMA, -1e20 },
	{ F_LGAMMA, -4503599627370497.0 },
	{ F_J0, 0.0 }, { F_J0, -0.0 }, { F_J0, I }, { F_J0, -I }, { F_J0, N }, { F_J0, 1e300 },
	{ F_J1, 0.0 }, { F_J1, -0.0 }, { F_J1, I }, { F_J1, N }, { F_J1, -1e300 },
	{ F_Y0, 0.0 }, { F_Y0, -0.0 }, { F_Y0, -1 }, { F_Y0, I }, { F_Y0, -I }, { F_Y0, N },
	{ F_Y0, 1e300 }, { F_Y0, 1e-310 },
	{ F_Y1, 0.0 }, { F_Y1, -0.0 }, { F_Y1, -1 }, { F_Y1, I }, { F_Y1, N }, { F_Y1, 1e300 },
	{ F_Y1, 1e-310 },
	{ F_JN, 2, 0.0 }, { F_JN, 3, -0.0 }, { F_JN, 2, I }, { F_JN, 5, N }, { F_JN, 0, 0.0 },
	{ F_JN, 1, -0.0 }, { F_JN, -1, 0.0 }, { F_YN, 2, 0.0 }, { F_YN, 2, -1 }, { F_YN, 3, I },
	{ F_YN, 0, 2.5 }, { F_YN, -1, 2.5 },
};
#define NSPECS (int) (sizeof specs / sizeof specs[0])

static int same(double a, double b, int fl)
{
	if (isnan(a) && isnan(b))
		return 1;
	if (isfinite(b) && b != 0 && fabs(a - b) <= (fl ? nextafterf(fabs((float) b), INFINITY) - fabs((float) b)
	                                                 : nextafter(fabs(b), INFINITY) - fabs(b)))
		return 1;
	if (fl)
		return (float) a == (float) b && signbit(a) == signbit(b);
	return memcmp(&a, &b, sizeof a) == 0;
}

static uint64_t bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* one case for test060-expected.h: function, x, y, PC result, reference, signgam */
static void emit(FILE *g, int f, double x, double y, double r, double ref, int sg, int special)
{
	if (isfloat[f])
		fprintf(g, "\t{ %d, %d, { 0x%08x, 0 }, { 0x%08x, 0 }, { 0x%08x, 0 }, { 0x%08x, 0 }, %d },\n",
			f, special, fbits((float) x), fbits((float) y), fbits((float) r),
			fbits((float) ref), sg);
	else
		fprintf(g, "\t{ %d, %d, { 0x%08x, 0x%08x }, { 0x%08x, 0x%08x }, { 0x%08x, 0x%08x }, { 0x%08x, 0x%08x }, %d },\n",
			f, special, (unsigned) (bits(x) >> 32), (unsigned) bits(x),
			(unsigned) (bits(y) >> 32), (unsigned) bits(y),
			(unsigned) (bits(r) >> 32), (unsigned) bits(r),
			(unsigned) (bits(ref) >> 32), (unsigned) bits(ref), sg);
}

int main(int argc, char **argv)
{
	int nrand = 20000, i, j, f, bad = 0, keep = 6;
	const char *gen = 0;
	FILE *g = 0;
	double maxl[NF] = { 0 }, maxg[NF] = { 0 }, atl[NF][2], atg[NF][2];
	long count[NF] = { 0 }, sgbad[NF] = { 0 }, over2[NF] = { 0 }, over2g[NF] = { 0 };

	for (i = 1; i < argc; i++)
		if (!strcmp(argv[i], "-n") && i + 1 < argc) nrand = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-g") && i + 1 < argc) gen = argv[++i];
	if (gen) {
		g = fopen(gen, "w");
		if (!g) { perror(gen); return 2; }
		fprintf(g, "/* test060-expected.h -- generated by `make expected` (native060 -g) on the PC:\n"
			"   f, special, x, y, PC result, reference, signgam (lgamma); doubles as\n"
			"   high/low words, floats in the first.  A special's reference is glibc's\n"
			"   result, a random case's MPFR's correctly rounded value. */\n"
			"static const struct tcase {\n\tshort f, special;\n"
			"\tunsigned long x[2], y[2], r[2], ref[2];\n\tint sg;\n} tcases[] = {\n");
	}

	/* the aliases are the same functions */
	if (m060_gamma(-2.5) != m060_lgamma(-2.5) || m060__lgamma(3.5) != m060_lgamma(3.5) ||
	    m060__j0(2) != m060_j0(2) || m060__j1(2) != m060_j1(2) || m060__y0(2) != m060_y0(2) ||
	    m060__y1(2) != m060_y1(2) || m060__jn(3, 2) != m060_jn(3, 2) || m060__yn(3, 2) != m060_yn(3, 2) || m060__erf(0.3) != m060_erf(0.3) ||
	    m060__erfc(0.3) != m060_erfc(0.3)) {
		printf("aliases: FAIL\n");
		bad++;
	}

	for (i = 0; i < NSPECS; i++) {
		int sl = 0, sgl = 0, fl;
		double rl, rg;

		f = specs[i].f; fl = isfloat[f];
		m060_signgam = 0; signgam = 0;
		rl = call(f, specs[i].x, specs[i].y, 1, &sl);
		rg = call(f, specs[i].x, specs[i].y, 0, &sgl);
		if (!same(rl, rg, fl) || (f == F_LGAMMA && sl != sgl)) {
			printf("special %s(%g%s%g): lib060 %a (signgam %d), glibc %a (signgam %d)\n",
			       fname[f], specs[i].x, arity[f] == 2 ? ", " : "", arity[f] == 2 ? specs[i].y : 0.0,
			       rl, sl, rg, sgl);
			bad++;
		}
		if (g)
			emit(g, f, specs[i].x, specs[i].y, rl, rg, f == F_LGAMMA ? sgl : 0, 1);
	}

	for (j = 0; j < NRANGES; j++) {
		const struct range *rg = &ranges[j];
		f = rg->f;
		for (i = 0; i < nrand; i++) {
			double x = pick(rg->kx, rg->ax, rg->bx), y = 0, rl, rgl, el, eg;
			int sl = 0, sgl = 0, sge = 0;
			mpfr_t e;

			if (arity[f] == 2)
				y = pick(rg->ky, rg->ay, rg->by);
			if (isfloat[f]) { x = (float) x; y = (float) y; }
			if (f == F_JN || f == F_YN)
				x = trunc(x);
			if (f == F_POW && x < 0)
				y = rint(y);
			mpfr_init2(e, 256);
			exact(f, x, y, e, &sge);
			rl = call(f, x, y, 1, &sl);
			rgl = call(f, x, y, 0, &sgl);
			el = ulps(rl, e, isfloat[f]);
			eg = ulps(rgl, e, isfloat[f]);
			if (f == F_LGAMMA && sl != sge && mpfr_get_d(e, RNDN) != INFINITY)
				sgbad[f]++;
			over2[f] += el > 2; over2g[f] += eg > 2;
			if (el > maxl[f]) { maxl[f] = el; atl[f][0] = x; atl[f][1] = y; }
			if (eg > maxg[f]) { maxg[f] = eg; atg[f][0] = x; atg[f][1] = y; }
			count[f]++;
			if (g && i < keep)
				emit(g, f, x, y, rl, isfloat[f] ? (float) mpfr_get_d(e, RNDN) : mpfr_get_d(e, RNDN),
				     f == F_LGAMMA ? sge : 0, 0);
			mpfr_clear(e);
		}
	}
	if (g) {
		fprintf(g, "};\n");
		fclose(g);
	}

	printf("%-7s %8s  %8s %8s %-28s  %8s %8s %s\n", "", "cases", "lib060", ">2 ulp", "(worst at)",
	       "glibc", ">2 ulp", "(worst at)");
	for (f = 0; f < NF; f++) {
		char a[64], b[64];
		snprintf(a, sizeof a, arity[f] == 2 ? "%.6g, %.6g" : "%.10g", atl[f][0], atl[f][1]);
		snprintf(b, sizeof b, arity[f] == 2 ? "%.6g, %.6g" : "%.10g", atg[f][0], atg[f][1]);
		printf("%-7s %8ld  %8.3g %8ld (%-26s)  %8.3g %8ld (%s)%s\n", fname[f], count[f], maxl[f], over2[f], a,
		       maxg[f], over2g[f], b,
		       sgbad[f] ? "  signgam wrong" : "");
		if (sgbad[f])
			bad++;
	}
	printf("specials: %d, %s\n", NSPECS, bad ? "FAIL" : "all as glibc");
	return bad != 0;
}
