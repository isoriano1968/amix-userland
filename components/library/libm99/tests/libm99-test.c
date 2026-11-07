/*
 * libm99-test.c -- libm99.so.1 on AMIX: every exported function against
 * glibc's results (computed on the PC, tests/libm99-expected.h), within the
 * tolerance of tests/cases.h, and how many results are bit for bit what the
 * same musl code gives on the PC.  Then by hand: nan*, the classification
 * and comparison macros, the constants, and <fenv.h> (rounding modes,
 * exception flags, the FPCR a process starts with).
 *
 *   LD_LIBRARY_PATH=. ./libm99-test [-v]
 *
 * -v prints every entry, not only the failing ones.  Exit 0 when all pass.
 * Built by `make test-amix` (gcc 14, -fno-builtin so that every call goes
 * to the library); needs nothing but AMIX's libc and libm.
 */
#include <math.h>
#include <complex.h>
#include <fenv.h>
#include <float.h>
#include <stdio.h>
#include <string.h>

#include "cases.h"

#define INF_L __builtin_infl()
#define NAN_L __builtin_nanl("")

struct row { int e; long double a[4], g[2], m[2]; };
static const struct row rows[] = {
#include "libm99-expected.h"
};
#define NROWS (int)(sizeof rows / sizeof rows[0])

typedef void (*vfn)(void);
struct ent { const char *name; char kind, ty; vfn f; double tol; };

#define E(k, t, n, tl) { #n, k, #t[0], (vfn)n, tl },
#define F1(t, n, m, a, b, c, d, tl)  E('1', t, n, tl)
#define F2(t, n, m, a, b, c, d, tl)  E('2', t, n, tl)
#define F3(t, n, m, a, b, c, d, tl)  E('3', t, n, tl)
#define FI(t, n, m, a, b, c, d, tl)  E('I', t, n, tl)
#define FL(t, n, m, a, b, c, d, tl)  E('L', t, n, tl)
#define FLL(t, n, m, a, b, c, d, tl) E('Q', t, n, tl)
#define FPI(t, n, m, a, b, c, d, tl) E('P', t, n, tl)
#define FRQ(t, n, m, a, b, c, d, tl) E('R', t, n, tl)
#define FMF(t, n, m, a, b, c, d, tl) E('M', t, n, tl)
#define FXI(t, n, m, a, b, c, d, tl) E('X', t, n, tl)
#define FXL(t, n, m, a, b, c, d, tl) E('Y', t, n, tl)
#define FNT(t, n, m, a, b, c, d, tl) E('N', t, n, tl)
#define C1(t, n, m, a, b, c, d, tl)  E('C', t, n, tl)
#define CR(t, n, m, a, b, c, d, tl)  E('A', t, n, tl)
#define C2(t, n, m, a, b, c, d, tl)  E('W', t, n, tl)
static const struct ent ents[] = {
	CASES_F1 CASES_F2 CASES_F3 CASES_FI CASES_FL CASES_FLL CASES_FPI CASES_FRQ CASES_FMF
	CASES_FXI CASES_FXL CASES_FNT CASES_C1 CASES_CR CASES_C2
};
#define NENTS (int)(sizeof ents / sizeof ents[0])

/* the m68k extended format: sign+exponent, a pad word, 64-bit mantissa
 * with an explicit integer bit */
union ldbits { long double f; struct { unsigned short se, pad; unsigned long hi, lo; } i; };

static int isnan_l(long double x) { return x != x; }
static int isinf_l(long double x) { return x == INF_L || x == -INF_L; }
static int finite_l(long double x) { return !isnan_l(x) && !isinf_l(x); }
static int sign_l(long double x) { union ldbits u; u.f = x; return u.i.se >> 15; }

/* one unit in the last place of w in a type of p bits, minimum exponent emin */
static long double ulp_of(long double w, int p, int emin)
{
	union ldbits u;
	u.f = w;
	int e = (u.i.se & 0x7fff) - 16383, b;
	if (w == 0 || e < emin)
		e = emin;
	b = e - p + 1 + 16383;
	u.i.se = b < 1 ? 1 : b;
	u.i.hi = 0x80000000UL;
	u.i.lo = 0;
	return u.f;
}

static double ulps(long double got, long double want, int p, int emin)
{
	if (isnan_l(want) || isnan_l(got))
		return isnan_l(want) && isnan_l(got) ? 0 : 1e30;
	if (isinf_l(want) || isinf_l(got))
		return got == want ? 0 : 1e30;
	if (want == 0 && got == 0)
		return sign_l(want) == sign_l(got) ? 0 : 1e30;
	long double d = got - want;
	if (d < 0)
		d = -d;
	return (double)(d / ulp_of(want, p, emin));
}

/* as native-test.c's cerr: normwise, signs of zero not judged */
static double cerr(long double rr, long double ri, long double gr, long double gi, int p, int emin)
{
	if (isnan_l(gr) || isnan_l(gi) || isinf_l(gr) || isinf_l(gi) ||
	    isnan_l(rr) || isnan_l(ri) || isinf_l(rr) || isinf_l(ri)) {
		int ginf = isinf_l(gr) || isinf_l(gi), rinf = isinf_l(rr) || isinf_l(ri);
		if (ginf || rinf)
			return ginf == rinf ? 0 : 1e30;
		return (isnan_l(gr) || isnan_l(gi)) == (isnan_l(rr) || isnan_l(ri)) ? 0 : 1e30;
	}
	long double big = gr < 0 ? -gr : gr, bi = gi < 0 ? -gi : gi, u, d1, d2;
	if (bi > big)
		big = bi;
	if (big == 0)
		return rr == 0 && ri == 0 ? 0 : 1e30;
	u = ulp_of(big, p, emin);
	d1 = rr - gr; d2 = ri - gi;
	if (d1 < 0) d1 = -d1;
	if (d2 < 0) d2 = -d2;
	return (double)((d1 > d2 ? d1 : d2) / u);
}

static int same_bits(long double a, long double b)
{
	if (isnan_l(a) || isnan_l(b))
		return isnan_l(a) && isnan_l(b);
	return memcmp(&a, &b, 10) == 0;
}

/* call entry e on row r: res[0] the result (the real part), res[1] the
 * second output (imaginary part, *quo, *exp, *sign, *iptr) */
static void call(const struct ent *e, const long double *a, long double *res)
{
	int iv = 0;
	res[1] = 0;
#define CALLT(T, CT) \
	switch (e->kind) { \
	case '1': res[0] = ((T (*)(T))e->f)(a[0]); break; \
	case '2': res[0] = ((T (*)(T, T))e->f)(a[0], a[1]); break; \
	case '3': res[0] = ((T (*)(T, T, T))e->f)(a[0], a[1], a[2]); break; \
	case 'I': res[0] = ((int (*)(T))e->f)(a[0]); break; \
	case 'L': res[0] = ((long (*)(T))e->f)(a[0]); break; \
	case 'Q': res[0] = ((long long (*)(T))e->f)(a[0]); break; \
	case 'P': res[0] = ((T (*)(T, int *))e->f)(a[0], &iv); res[1] = iv; break; \
	case 'R': res[0] = ((T (*)(T, T, int *))e->f)(a[0], a[1], &iv); \
		res[1] = iv < 0 ? -(-iv & 7) : iv & 7; break; \
	case 'M': { T ip = 0; res[0] = ((T (*)(T, T *))e->f)(a[0], &ip); res[1] = ip; break; } \
	case 'X': res[0] = ((T (*)(T, int))e->f)(a[0], (int)a[1]); break; \
	case 'Y': res[0] = ((T (*)(T, long))e->f)(a[0], (long)a[1]); break; \
	case 'N': res[0] = ((T (*)(T, long double))e->f)(a[0], a[1]); break; \
	case 'C': { CT z = CMPLXL(a[0], a[1]); z = ((CT (*)(CT))e->f)(z); \
		res[0] = creall(z); res[1] = cimagl(z); break; } \
	case 'A': { CT z = CMPLXL(a[0], a[1]); res[0] = ((T (*)(CT))e->f)(z); break; } \
	case 'W': { CT z = CMPLXL(a[0], a[1]), w = CMPLXL(a[2], a[3]); \
		z = ((CT (*)(CT, CT))e->f)(z, w); res[0] = creall(z); res[1] = cimagl(z); break; } \
	}
	if (e->ty == 'd') { CALLT(double, double complex) }
	else if (e->ty == 'f') { CALLT(float, float complex) }
	else { CALLT(long double, long double complex) }
}

static int checks, failed;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failed++;
		printf("FAIL %s\n", what);
	}
}

static void by_hand(void)
{
	volatile double zero = 0, one = 1, dnan;
	volatile float fz = 0;
	volatile long double lz = 0;
	fenv_t env;
	fexcept_t fl;

	/* nan*, the macros, the constants */
	check(isnan(nan("")) && isnan(nanf("")) && isnan(nanl("")), "nan nanf nanl");
	dnan = zero / zero;
	check(isnan(dnan) && !isnan(one) && isnan((float)dnan) && isnan((long double)dnan), "isnan");
	check(isinf(HUGE_VAL) && isinf(HUGE_VALF) && isinf(HUGE_VALL) && isinf(INFINITY) && !isinf(one), "isinf");
	check(isinf(-INFINITY) && signbit(-INFINITY) && !signbit(INFINITY), "isinf/signbit -inf");
	check(fpclassify(zero) == FP_ZERO && fpclassify(one) == FP_NORMAL && fpclassify(dnan) == FP_NAN &&
		fpclassify(HUGE_VAL) == FP_INFINITE && fpclassify(DBL_MIN / 4) == FP_SUBNORMAL, "fpclassify double");
	check(fpclassify(fz) == FP_ZERO && fpclassify(FLT_MIN / 4) == FP_SUBNORMAL && fpclassify(HUGE_VALF) == FP_INFINITE,
		"fpclassify float");
	check(fpclassify(lz) == FP_ZERO && fpclassify((long double)one) == FP_NORMAL && fpclassify(HUGE_VALL) == FP_INFINITE &&
		fpclassify(LDBL_MIN / 4) == FP_SUBNORMAL, "fpclassify long double");
	check(__fpclassify(one) == FP_NORMAL && __fpclassifyf(FLT_MIN / 4) == FP_SUBNORMAL &&
		__fpclassifyl(LDBL_MIN / 4) == FP_SUBNORMAL && __fpclassifyl(dnan) == FP_NAN &&
		__fpclassifyl(HUGE_VALL) == FP_INFINITE && __fpclassifyl(LDBL_MIN) == FP_NORMAL, "__fpclassify functions");
	check(__signbit(-zero) && !__signbit(zero) && __signbitf(-fz) && __signbitl(-lz), "__signbit functions");
	check(isfinite(one) && !isfinite(dnan) && !isfinite(HUGE_VAL) && isnormal(one) && !isnormal(zero), "isfinite isnormal");
	check(!isgreater(dnan, one) && !isless(dnan, one) && isunordered(dnan, one) && isgreater(2.0, one) &&
		islessgreater(one, 2.0) && !islessgreater(one, one), "comparison macros");
	check(finite(one) && !finite(HUGE_VAL) && finitef(1.0f), "finite finitef");
	check(M_PI > 3.14159 && M_PI < 3.1416 && FP_ILOGB0 == ilogb(zero) && FP_ILOGBNAN == ilogb(dnan), "M_PI, FP_ILOGB*");
	check(sizeof(float_t) == sizeof(float) && sizeof(double_t) == sizeof(double), "float_t, double_t (FLT_EVAL_METHOD 0)");

	/* <fenv.h>: what a process starts with, then each rounding mode */
	fegetenv(&env);
	printf("FPCR at start 0x%08x, FPSR 0x%08x (AMIX should give FPCR 0)\n",
		env.__control_register, env.__status_register);
	check((env.__control_register & 0xf0) == 0, "FPCR at start: extended precision, to nearest");
	check(fegetround() == FE_TONEAREST, "fegetround at start");
	check(fesetround(FE_UPWARD) == 0 && fegetround() == FE_UPWARD && nearbyint(0.5) == 1 && lrint(-1.5) == -1 &&
		rintf(0.25f) == 1 && rintl(0.25L) == 1, "FE_UPWARD");
	check(fesetround(FE_DOWNWARD) == 0 && nearbyint(0.5) == 0 && lrint(-0.5) == -1 && llrint(-0.5) == -1 &&
		rintl(-0.25L) == -1, "FE_DOWNWARD");
	check(fesetround(FE_TOWARDZERO) == 0 && nearbyint(-1.75) == -1 && lrintf(1.75f) == 1, "FE_TOWARDZERO");
	check(fesetround(FE_TONEAREST) == 0 && nearbyint(2.5) == 2 && nearbyint(3.5) == 4 && lrintl(2.5L) == 2,
		"FE_TONEAREST");
	feclearexcept(FE_ALL_EXCEPT);
	check(fetestexcept(FE_ALL_EXCEPT) == 0, "feclearexcept");
	dnan = one / zero;
	check(fetestexcept(FE_DIVBYZERO) && !fetestexcept(FE_INVALID), "1/0 raises FE_DIVBYZERO");
	dnan = zero / zero;
	check(fetestexcept(FE_INVALID) != 0, "0/0 raises FE_INVALID");
	feclearexcept(FE_ALL_EXCEPT);
	(void)nearbyint(0.5);
	check(!fetestexcept(FE_INEXACT), "nearbyint leaves FE_INEXACT");
	(void)rintf(0.5f);
	check(fetestexcept(FE_INEXACT) != 0, "rintf raises FE_INEXACT");
	feclearexcept(FE_ALL_EXCEPT);
	(void)log2(zero);
	check(fetestexcept(FE_DIVBYZERO) != 0, "log2(0) raises FE_DIVBYZERO");
	feraiseexcept(FE_OVERFLOW);
	fegetexceptflag(&fl, FE_ALL_EXCEPT);
	feclearexcept(FE_ALL_EXCEPT);
	fesetexceptflag(&fl, FE_OVERFLOW);
	check(fetestexcept(FE_ALL_EXCEPT) == (FE_OVERFLOW | FE_DIVBYZERO) ||
		fetestexcept(FE_ALL_EXCEPT) == FE_OVERFLOW, "fegetexceptflag/fesetexceptflag");
	feholdexcept(&env);
	check(fetestexcept(FE_ALL_EXCEPT) == 0, "feholdexcept clears");
	feraiseexcept(FE_UNDERFLOW);
	feupdateenv(&env);
	check(fetestexcept(FE_UNDERFLOW) && fetestexcept(FE_OVERFLOW), "feupdateenv merges");
	fesetenv(FE_DFL_ENV);
	check(fegetround() == FE_TONEAREST && fetestexcept(FE_ALL_EXCEPT) == 0, "fesetenv(FE_DFL_ENV)");
}

int main(int argc, char **argv)
{
	int verbose = argc > 1 && !strcmp(argv[1], "-v");
	int r = 0, nbad = 0, nsame = 0, ntotal = 0, nannex = 0;

	printf("libm99-test: %d function entries, %d cases\n", NENTS, NROWS);
	for (int i = 0; i < NENTS; i++) {
		const struct ent *e = &ents[i];
		int p = e->ty == 'f' ? 24 : e->ty == 'd' ? 53 : 64;
		int emin = e->ty == 'f' ? -126 : e->ty == 'd' ? -1022 : -16382;
		double worst = 0;
		int n = 0, bad = 0, same = 0;
		const struct row *wr = 0;
		for (; r < NROWS && rows[r].e == i; r++) {
			const struct row *w = &rows[r];
			long double res[2] = { 0, 0 };
			double err;
			call(e, w->a, res);
			switch (e->kind) {
			case 'I': case 'L': case 'Q':
				err = res[0] == w->g[0] ? 0 : 1e30; break;
			case 'C': case 'W':
				err = cerr(res[0], res[1], w->g[0], w->g[1], p, emin); break;
			case 'P': case 'R':
				err = ulps(res[0], w->g[0], p, emin);
				/* the sign at a pole of lgamma is not specified */
				if (!isnan_l(w->g[0]) && !isinf_l(w->g[0]) && res[1] != w->g[1])
					err = 1e30;
				break;
			case 'M': {
				double e2 = ulps(res[1], w->g[1], p, emin);
				err = ulps(res[0], w->g[0], p, emin);
				if (e2 > err)
					err = e2;
				break;
			}
			default:
				err = ulps(res[0], w->g[0], p, emin);
			}
			/* C99 Annex G (an infinite or NaN part): musl does not follow
			 * it everywhere, so these are counted, not judged */
			if (strchr("CAW", e->kind) && err > 0 && (!finite_l(w->a[0]) || !finite_l(w->a[1]) ||
			    !finite_l(w->a[2]) || !finite_l(w->a[3]))) {
				nannex++;
				err = 0;
			}
			n++;
			if (same_bits(res[0], w->m[0]) && same_bits(res[1], w->m[1]))
				same++;
			if (err > e->tol)
				bad++;
			if (err > worst || !wr) {
				worst = err > worst ? err : worst;
				wr = w;
			}
			if (err > e->tol && verbose)
				printf("  %s(%.17g, %.17g): %.17g %.17g, glibc %.17g %.17g\n", e->name,
					(double)w->a[0], (double)w->a[1], (double)res[0], (double)res[1],
					(double)w->g[0], (double)w->g[1]);
		}
		if (bad || verbose)
			printf("%-12s %3d cases, max %.3g ulp (tol %g), %d/%d as on the PC%s\n", e->name, n,
				worst, e->tol, same, n, bad ? "  FAIL" : "");
		nbad += bad != 0;
		nsame += same;
		ntotal += n;
	}
	printf("%d of %d entries over tolerance; %d of %d results bit-identical to musl on the PC;\n"
		"%d complex results at infinite/NaN inputs differ from glibc (Annex G, not judged)\n",
		nbad, NENTS, nsame, ntotal, nannex);
	by_hand();
	printf("%d of %d hand checks failed\n", failed, checks);
	return nbad || failed;
}
