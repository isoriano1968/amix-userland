/*
 * native-test.c -- libm99's musl sources, built for the PC, against glibc.
 *
 * `make native-test` compiles the same musl files as the AMIX library for
 * x86-64 (generic C only, no x86 assembler except fenv), keeps the same
 * exports and renames each to m99_<name>.  This program calls both for every
 * case in cases.h -- the specials and NRAND random inputs -- and prints the
 * largest error in ulps against glibc.  It proves the extraction and the
 * build, not the m68k code: the AMIX test is libm99-test.c.
 *
 *   native-test [-n NRAND] [-g FILE [-k NKEEP]]
 *
 * -g writes FILE, the cases for libm99-test.c: the specials and the first
 * NKEEP random inputs of each entry, with glibc's result and musl's (on the
 * PC; the same algorithms on AMIX should give the same bits).
 */
#define _GNU_SOURCE
#include <complex.h>
#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fenv.h>

#include "cases.h"

typedef double complex dcomplex;
typedef float complex fcomplex;
typedef long double complex lcomplex;

#define TY_d double
#define TY_f float
#define TY_l long double
#define CT_d dcomplex
#define CT_f fcomplex
#define CT_l lcomplex
#define P_d 53
#define P_f 24
#define P_l 64
#define EMIN_d (-1022)
#define EMIN_f (-126)
#define EMIN_l (-16382)

/* the renamed musl functions */
#define F1(t, n, m, a, b, c, d, tol)  TY_##t m99_##n(TY_##t);
#define F2(t, n, m, a, b, c, d, tol)  TY_##t m99_##n(TY_##t, TY_##t);
#define F3(t, n, m, a, b, c, d, tol)  TY_##t m99_##n(TY_##t, TY_##t, TY_##t);
#define FI(t, n, m, a, b, c, d, tol)  int m99_##n(TY_##t);
#define FL(t, n, m, a, b, c, d, tol)  long m99_##n(TY_##t);
#define FLL(t, n, m, a, b, c, d, tol) long long m99_##n(TY_##t);
#define FPI(t, n, m, a, b, c, d, tol) TY_##t m99_##n(TY_##t, int *);
#define FRQ(t, n, m, a, b, c, d, tol) TY_##t m99_##n(TY_##t, TY_##t, int *);
#define FMF(t, n, m, a, b, c, d, tol) TY_##t m99_##n(TY_##t, TY_##t *);
#define FXI(t, n, m, a, b, c, d, tol) TY_##t m99_##n(TY_##t, int);
#define FXL(t, n, m, a, b, c, d, tol) TY_##t m99_##n(TY_##t, long);
#define FNT(t, n, m, a, b, c, d, tol) TY_##t m99_##n(TY_##t, long double);
#define C1(t, n, m, a, b, c, d, tol)  CT_##t m99_##n(CT_##t);
#define CR(t, n, m, a, b, c, d, tol)  TY_##t m99_##n(CT_##t);
#define C2(t, n, m, a, b, c, d, tol)  CT_##t m99_##n(CT_##t, CT_##t);
CASES_F1 CASES_F2 CASES_F3 CASES_FI CASES_FL CASES_FLL CASES_FPI CASES_FRQ CASES_FMF
CASES_FXI CASES_FXL CASES_FNT CASES_C1 CASES_CR CASES_C2
#undef F1
#undef F2
#undef F3
#undef FI
#undef FL
#undef FLL
#undef FPI
#undef FRQ
#undef FMF
#undef FXI
#undef FXL
#undef FNT
#undef C1
#undef CR
#undef C2

/* glibc has no finite in <math.h> without _DEFAULT_SOURCE, scalb is gone */
int finite(double);
int finitef(float);
double scalb(double, double);
float scalbf(float, float);

static unsigned long long rng = 0x9e3779b97f4a7c15ULL;
static double rnd01(void)
{
	rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
	return (rng >> 11) * (1.0 / 9007199254740992.0);
}

static long double sample(char mode, double lo, double hi)
{
	if (mode == 'U')
		return lo + (long double)(hi - lo) * rnd01() + (long double)rnd01() * 0x1p-60L * (hi - lo);
	long double m = 1 + (long double)rnd01() + rnd01() * 0x1p-53L;
	int e = (int)lo + (int)(rnd01() * (hi - lo));
	m = ldexpl(m, e);
	if (mode == 'E' && rnd01() < 0.5)
		m = -m;
	return m;
}

/* the error of got against want, in ulps of a type with p bits and minimum
 * exponent emin; NaN/Inf/signed zero must match exactly */
static double ulps(long double got, long double want, int p, int emin)
{
	if (isnan(want) || isnan(got))
		return isnan(want) && isnan(got) ? 0 : 1e30;
	if (isinf(want) || isinf(got))
		return got == want ? 0 : 1e30;
	if (want == 0 && got == 0)
		return signbit(want) == signbit(got) ? 0 : 1e30;
	int e = want == 0 ? emin : ilogbl(want);
	if (e < emin)
		e = emin;
	return (double)(fabsl(got - want) / ldexpl(1, e - p + 1));
}

static const long double specials[] = { 0.0L, -0.0L, 1.0L, -1.0L, 0.5L, -2.5L, 3.0L,
	INFINITY, -INFINITY, NAN };
#define NSPECIAL (int)(sizeof specials / sizeof specials[0])

static int nrand = 400, nkeep = 8;
static FILE *gen;
static int idx, nfail, nfuncs;

/* one entry's result line, and its rows in the generated file */
struct acc { const char *name; double worst; long double wa[4]; int n, bad; double tol; double fworst; long double fa[4]; int nclass, nannex; };

static void acc_add(struct acc *a, double e, const long double *args)
{
	a->n++;
	if (e > a->tol)
		a->bad++;
	if (e >= 1e30)
		a->nclass++;
	else if (e > a->fworst) {
		a->fworst = e;
		memcpy(a->fa, args, sizeof a->fa);
	}
	if (e > a->worst) {
		a->worst = e;
		memcpy(a->wa, args, sizeof a->wa);
	}
}

static void acc_done(struct acc *a)
{
	printf("%-12s %5d  max %-10.3g tol %-6g %s", a->name, a->n, a->fworst, a->tol,
		a->bad ? "FAIL" : "ok");
	if (a->bad)
		printf("  (%d over, %d NaN/Inf/zero-sign mismatches; finite worst at %Lg %Lg %Lg %Lg; mismatch at %La %La %La %La)",
			a->bad, a->nclass, a->fa[0], a->fa[1], a->fa[2], a->fa[3], a->wa[0], a->wa[1], a->wa[2], a->wa[3]);
	if (a->nannex)
		printf("  [%d Annex G inputs differ from glibc]", a->nannex);
	printf("\n");
	nfail += a->bad != 0;
	nfuncs++;
	idx++;
}

static void emit_ld(long double v)
{
	if (isnan(v))
		fputs("NAN_L", gen);
	else if (isinf(v))
		fputs(v < 0 ? "-INF_L" : "INF_L", gen);
	else
		fprintf(gen, "%LaL", v);
}

static void emit(const long double *args, long double g0, long double g1, long double m0, long double m1)
{
	if (!gen)
		return;
	fprintf(gen, "\t{ %d, { ", idx);
	for (int i = 0; i < 4; i++) {
		emit_ld(args[i]);
		fputs(i < 3 ? ", " : " }, { ", gen);
	}
	emit_ld(g0); fputs(", ", gen); emit_ld(g1); fputs(" }, { ", gen);
	emit_ld(m0); fputs(", ", gen); emit_ld(m1); fputs(" } },\n", gen);
}

/* the inputs of one entry: specials first (k < NSPECIAL), then random ones;
 * the generated file keeps the specials and the first nkeep random */
#define LOOP(nargs, usespec) \
	for (int k = (usespec) ? 0 : NSPECIAL; k < NSPECIAL + nrand; k++) { \
		long double A[4] = { 0, 0, 0, 0 }; int keep = k < NSPECIAL + nkeep; \
		for (int j = 0; j < (nargs); j++) A[j] = k < NSPECIAL ? specials[(k + 3 * j) % NSPECIAL] \
			: sample(mode_, j & 1 ? lo2_ : lo_, j & 1 ? hi2_ : hi_);
#define ENDLOOP }

#define T(t, x) ((TY_##t)(x))
#define HEAD(n, m, a, b, c, d, tl) { struct acc ac = { #n, 0, {0}, 0, 0, tl }; char mode_ = #m[0]; \
	double lo_ = a, hi_ = b, lo2_ = c, hi2_ = d; (void)mode_; (void)lo_; (void)hi_; (void)lo2_; (void)hi2_;

#define F1(t, n, m, a, b, c, d, tl) HEAD(n, m, a, b, c, d, tl) LOOP(1, 1) \
	TY_##t g = n(T(t, A[0])), r = m99_##n(T(t, A[0])); A[0] = T(t, A[0]); \
	acc_add(&ac, ulps(r, g, P_##t, EMIN_##t), A); if (keep) emit(A, g, 0, r, 0); ENDLOOP acc_done(&ac); }
#define F2(t, n, m, a, b, c, d, tl) HEAD(n, m, a, b, c, d, tl) LOOP(2, 1) \
	A[0] = T(t, A[0]); A[1] = T(t, A[1]); \
	TY_##t g = n(A[0], A[1]), r = m99_##n(A[0], A[1]); \
	acc_add(&ac, ulps(r, g, P_##t, EMIN_##t), A); if (keep) emit(A, g, 0, r, 0); ENDLOOP acc_done(&ac); }
#define F3(t, n, m, a, b, c, d, tl) HEAD(n, m, a, b, c, d, tl) LOOP(3, 1) \
	A[0] = T(t, A[0]); A[1] = T(t, A[1]); A[2] = T(t, A[2]); \
	TY_##t g = n(A[0], A[1], A[2]), r = m99_##n(A[0], A[1], A[2]); \
	acc_add(&ac, ulps(r, g, P_##t, EMIN_##t), A); if (keep) emit(A, g, 0, r, 0); ENDLOOP acc_done(&ac); }
#define FINT(t, n, m, a, b, c, d, tl, R, spec) HEAD(n, m, a, b, c, d, tl) LOOP(1, spec) \
	A[0] = T(t, A[0]); R g = n(A[0]), r = m99_##n(A[0]); \
	acc_add(&ac, g == r ? 0 : 1e30, A); if (keep) emit(A, g, 0, r, 0); ENDLOOP acc_done(&ac); }
#define FI(t, n, m, a, b, c, d, tl)  FINT(t, n, m, a, b, c, d, tl, int, 1)
#define FL(t, n, m, a, b, c, d, tl)  FINT(t, n, m, a, b, c, d, tl, long, 0)
#define FLL(t, n, m, a, b, c, d, tl) FINT(t, n, m, a, b, c, d, tl, long long, 0)
#define FPI(t, n, m, a, b, c, d, tl) HEAD(n, m, a, b, c, d, tl) LOOP(1, 1) \
	A[0] = T(t, A[0]); int gi = 0, ri = 0; TY_##t g = n(A[0], &gi), r = m99_##n(A[0], &ri); \
	double e = ulps(r, g, P_##t, EMIN_##t); if (!isnan(g) && !isinf(g) && gi != ri) e = 1e30; \
	acc_add(&ac, e, A); if (keep) emit(A, g, gi, r, ri); ENDLOOP acc_done(&ac); }
/* remquo: the sign and the low 3 bits of the quotient are what C99 promises */
#define FRQ(t, n, m, a, b, c, d, tl) HEAD(n, m, a, b, c, d, tl) LOOP(2, 1) \
	A[0] = T(t, A[0]); A[1] = T(t, A[1]); int gi = 0, ri = 0; \
	TY_##t g = n(A[0], A[1], &gi), r = m99_##n(A[0], A[1], &ri); \
	gi = gi < 0 ? -(-gi & 7) : gi & 7; ri = ri < 0 ? -(-ri & 7) : ri & 7; \
	double e = ulps(r, g, P_##t, EMIN_##t); if (!isnan(g) && gi != ri) e = 1e30; \
	acc_add(&ac, e, A); if (keep) emit(A, g, gi, r, ri); ENDLOOP acc_done(&ac); }
#define FMF(t, n, m, a, b, c, d, tl) HEAD(n, m, a, b, c, d, tl) LOOP(1, 1) \
	A[0] = T(t, A[0]); TY_##t gi, ri; TY_##t g = n(A[0], &gi), r = m99_##n(A[0], &ri); \
	double e = ulps(r, g, P_##t, EMIN_##t), e2 = ulps(ri, gi, P_##t, EMIN_##t); \
	acc_add(&ac, e > e2 ? e : e2, A); if (keep) emit(A, g, gi, r, ri); ENDLOOP acc_done(&ac); }
#define FXI(t, n, m, a, b, c, d, tl) HEAD(n, m, a, b, c, d, tl) LOOP(1, 1) \
	A[0] = T(t, A[0]); A[1] = (int)(c + (d - c) * rnd01()); \
	TY_##t g = n(A[0], (int)A[1]), r = m99_##n(A[0], (int)A[1]); \
	acc_add(&ac, ulps(r, g, P_##t, EMIN_##t), A); if (keep) emit(A, g, 0, r, 0); ENDLOOP acc_done(&ac); }
#define FXL(t, n, m, a, b, c, d, tl) HEAD(n, m, a, b, c, d, tl) LOOP(1, 1) \
	A[0] = T(t, A[0]); A[1] = (long)(c + (d - c) * rnd01()); \
	TY_##t g = n(A[0], (long)A[1]), r = m99_##n(A[0], (long)A[1]); \
	acc_add(&ac, ulps(r, g, P_##t, EMIN_##t), A); if (keep) emit(A, g, 0, r, 0); ENDLOOP acc_done(&ac); }
/* nexttoward: the second argument is long double; half the cases go to a
 * neighbour of x so that it matters */
#define FNT(t, n, m, a, b, c, d, tl) HEAD(n, m, a, b, c, d, tl) LOOP(2, 1) \
	A[0] = T(t, A[0]); if (k >= NSPECIAL && (k & 1)) A[1] = A[0] + A[0] * 0x1p-70L * (k & 2 ? 1 : -1); \
	TY_##t g = n(A[0], A[1]), r = m99_##n(A[0], A[1]); \
	acc_add(&ac, ulps(r, g, P_##t, EMIN_##t), A); if (keep) emit(A, g, 0, r, 0); ENDLOOP acc_done(&ac); }

/* an input with an infinite or NaN part: Annex G territory, counted only */
static double annex(struct acc *a, const long double *A, int n, double e)
{
	for (int i = 0; i < n; i++)
		if (!isfinite(A[i])) {
			if (e > 0)
				a->nannex++;
			return 0;
		}
	return e;
}

/* complex error: the larger part's error, both in ulps of the larger part of
 * glibc's result (a normwise error) */
static double cerr(long double rr, long double ri, long double gr, long double gi, int p, int emin)
{
	if (isnan(gr) || isnan(gi) || isinf(gr) || isinf(gi) || isnan(rr) || isnan(ri) || isinf(rr) || isinf(ri)) {
		/* C99 annex G: an infinite part is the answer, the other part may be NaN */
		int ginf = isinf(gr) || isinf(gi), rinf = isinf(rr) || isinf(ri);
		if (ginf || rinf)
			return ginf == rinf ? 0 : 1e30;
		return (isnan(gr) || isnan(gi)) == (isnan(rr) || isnan(ri)) ? 0 : 1e30;
	}
	long double big = fabsl(gr) > fabsl(gi) ? fabsl(gr) : fabsl(gi);
	double e1 = ulps(gr + (rr - gr), gr, p, emin), e2 = ulps(gi + (ri - gi), gi, p, emin);
	if (big != 0) {
		int e = ilogbl(big); if (e < emin) e = emin;
		long double u = ldexpl(1, e - p + 1);
		e1 = (double)(fabsl(rr - gr) / u); e2 = (double)(fabsl(ri - gi) / u);
		if (rr == 0 && gr == 0 && signbit(rr) != signbit(gr)) e1 = 0;	/* sign of zero: not judged */
		if (ri == 0 && gi == 0 && signbit(ri) != signbit(gi)) e2 = 0;
	}
	return e1 > e2 ? e1 : e2;
}

#define MKC(t, re, im) ((CT_##t)((TY_##t)(re)) + (CT_##t)((TY_##t)(im)) * I)
#define C1(t, n, m, a, b, c, d, tl) HEAD(n, m, a, b, a, b, tl) LOOP(2, 1) \
	A[0] = T(t, A[0]); A[1] = T(t, A[1]); CT_##t z = CMPLXL(A[0], A[1]); \
	CT_##t g = n(z), r = m99_##n(z); \
	acc_add(&ac, annex(&ac, A, 2, cerr(creall(r), cimagl(r), creall(g), cimagl(g), P_##t, EMIN_##t)), A); \
	if (keep) emit(A, creall(g), cimagl(g), creall(r), cimagl(r)); ENDLOOP acc_done(&ac); }
#define CR(t, n, m, a, b, c, d, tl) HEAD(n, m, a, b, a, b, tl) LOOP(2, 1) \
	A[0] = T(t, A[0]); A[1] = T(t, A[1]); CT_##t z = CMPLXL(A[0], A[1]); \
	TY_##t g = n(z), r = m99_##n(z); \
	acc_add(&ac, annex(&ac, A, 2, ulps(r, g, P_##t, EMIN_##t)), A); if (keep) emit(A, g, 0, r, 0); ENDLOOP acc_done(&ac); }
/* cpow: the base in the right half plane, |base| 0.1..4, exponent parts -3..3 */
#define C2(t, n, m, a, b, c, d, tl) HEAD(n, m, a, b, c, d, tl) LOOP(0, 0) \
	A[0] = T(t, sample('U', a, b)); A[1] = T(t, sample('U', -b, b)); \
	A[2] = T(t, sample('U', c, d)); A[3] = T(t, sample('U', c, d)); \
	CT_##t z = CMPLXL(A[0], A[1]), w = CMPLXL(A[2], A[3]); CT_##t g = n(z, w), r = m99_##n(z, w); \
	acc_add(&ac, cerr(creall(r), cimagl(r), creall(g), cimagl(g), P_##t, EMIN_##t), A); \
	if (keep) emit(A, creall(g), cimagl(g), creall(r), cimagl(r)); ENDLOOP acc_done(&ac); }

int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-n") && i + 1 < argc)
			nrand = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-k") && i + 1 < argc)
			nkeep = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-g") && i + 1 < argc) {
			if (!(gen = fopen(argv[++i], "w"))) { perror(argv[i]); return 2; }
		} else {
			fprintf(stderr, "usage: native-test [-n NRAND] [-g FILE [-k NKEEP]]\n");
			return 2;
		}
	}
	if (nkeep > nrand)
		nkeep = nrand;
	if (gen)
		fprintf(gen, "/* written by tests/native-test -g (make expected): glibc's and PC musl's results\n"
			" * for libm99-test.c; { entry, args, glibc, musl } */\n");
	CASES_F1 CASES_F2 CASES_F3 CASES_FI CASES_FL CASES_FLL CASES_FPI CASES_FRQ CASES_FMF
	CASES_FXI CASES_FXL CASES_FNT CASES_C1 CASES_CR CASES_C2
	printf("%d entries, %d over their tolerance\n", nfuncs, nfail);
	if (gen)
		fclose(gen);
	return nfail != 0;
}
