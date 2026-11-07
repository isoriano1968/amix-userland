/*
 * cases.h -- what the libm99 tests call, and over which inputs.
 *
 * An X-macro table: native-test.c (the musl sources built for the PC,
 * against glibc) and, through the header it writes, libm99-test.c (on AMIX)
 * both expand it.  Every function libm99 exports is here, except nan*, the
 * classification helpers and <fenv.h>, which libm99-test.c checks by hand.
 *
 *   KIND(type, name, mode, lo, hi, lo2, hi2, tol)
 *
 * type: d, f or l (double, float, long double; for the complex ones the
 * complex type of that base).  mode: U samples lo..hi uniformly, E draws a
 * random sign and a magnitude of 2^lo..2^hi, P is E with the sign always +.
 * lo2/hi2 are the second argument's range (same mode; the complex ones
 * take lo/hi for both parts); tol is the error
 * allowed against glibc in units in the last place.  The large ones are
 * musl's: casin/cacos/casinh/cacosh use the textbook formulas (~100 ulp),
 * and most long double complex functions compute in double (2^11 to 2^18
 * long double ulps); see files/include/complex.h.
 */

/* T f(T) */
#define CASES_F1 \
	F1(d, exp2,   U, -1070, 1023, 0,0, 2)  F1(f, exp2f,  U, -149, 127, 0,0, 2) \
	F1(l, exp2l,  U, -16440, 16383, 0,0, 2) \
	F1(d, expm1,  U, -40, 709, 0,0, 2)     F1(f, expm1f, U, -20, 88, 0,0, 2) \
	F1(l, expm1l, U, -50, 11356, 0,0, 3) \
	F1(d, expm1,  E, -60, 0, 0,0, 2)       F1(l, expm1l, E, -70, 0, 0,0, 3) \
	F1(d, log1p,  U, -0.999, 1e6, 0,0, 2)  F1(f, log1pf, U, -0.999, 1e6, 0,0, 2) \
	F1(l, log1pl, U, -0.999, 1e6, 0,0, 3) \
	F1(d, log1p,  E, -60, 0, 0,0, 2)       F1(l, log1pl, E, -70, 0, 0,0, 3) \
	F1(d, log2,   P, -1070, 1023, 0,0, 2)  F1(f, log2f,  P, -149, 127, 0,0, 2) \
	F1(l, log2l,  P, -16000, 16000, 0,0, 3) \
	F1(d, logb,   E, -1070, 1023, 0,0, 0)  F1(f, logbf,  E, -149, 127, 0,0, 0) \
	F1(l, logbl,  E, -16000, 16000, 0,0, 0) \
	F1(d, tgamma, U, -170.5, 171.5, 0,0, 16) F1(f, tgammaf, U, -34.5, 35, 0,0, 4) \
	F1(l, tgammal, U, -50.5, 1750, 0,0, 64) \
	F1(d, nearbyint, U, -1e6, 1e6, 0,0, 0) F1(f, nearbyintf, U, -1e6, 1e6, 0,0, 0) \
	F1(l, nearbyintl, U, -1e6, 1e6, 0,0, 0) \
	F1(f, rintf,  U, -1e6, 1e6, 0,0, 0)    F1(l, rintl,  U, -1e6, 1e6, 0,0, 0) \
	F1(d, round,  U, -1e6, 1e6, 0,0, 0)    F1(f, roundf, U, -1e6, 1e6, 0,0, 0) \
	F1(l, roundl, U, -1e6, 1e6, 0,0, 0) \
	F1(d, trunc,  E, -10, 70, 0,0, 0)      F1(f, truncf, E, -10, 30, 0,0, 0) \
	F1(l, truncl, E, -10, 70, 0,0, 0) \
	F1(f, acoshf, U, 1, 1e6, 0,0, 2)       F1(l, acoshl, U, 1, 1e6, 0,0, 3) \
	F1(f, asinhf, U, -1e6, 1e6, 0,0, 2)    F1(l, asinhl, U, -1e6, 1e6, 0,0, 3) \
	F1(f, atanhf, U, -0.999, 0.999, 0,0, 2) F1(l, atanhl, U, -0.999, 0.999, 0,0, 3) \
	F1(f, cbrtf,  E, -140, 120, 0,0, 1)    F1(l, cbrtl,  E, -16000, 16000, 0,0, 2) \
	F1(f, erff,   U, -6, 6, 0,0, 2)        F1(l, erfl,   U, -6, 6, 0,0, 3) \
	F1(f, erfcf,  U, -4, 10, 0,0, 4)       F1(l, erfcl,  U, -4, 100, 0,0, 8) \
	F1(f, lgammaf, U, 0.01, 1e6, 0,0, 8)   F1(l, lgammal, U, 0.01, 1e6, 0,0, 8) \
	F1(l, acosl,  U, -1, 1, 0,0, 3)        F1(l, asinl,  U, -1, 1, 0,0, 3) \
	F1(l, atanl,  U, -100, 100, 0,0, 3) \
	F1(l, cosl,   U, -100, 100, 0,0, 3)    F1(l, sinl,   U, -100, 100, 0,0, 3) \
	F1(l, tanl,   U, -100, 100, 0,0, 3)    F1(l, sinl,   E, -20, 60, 0,0, 3) \
	F1(l, coshl,  U, -11000, 11000, 0,0, 3) F1(l, sinhl, U, -11000, 11000, 0,0, 3) \
	F1(l, tanhl,  U, -30, 30, 0,0, 3) \
	F1(l, expl,   U, -11300, 11356, 0,0, 3) F1(l, logl,  P, -16000, 16000, 0,0, 3) \
	F1(l, log10l, P, -16000, 16000, 0,0, 3) \
	F1(l, fabsl,  E, -16000, 16000, 0,0, 0) F1(l, sqrtl, P, -16000, 16000, 0,0, 0) \
	F1(l, ceill,  U, -1e6, 1e6, 0,0, 0)    F1(l, floorl, U, -1e6, 1e6, 0,0, 0)

/* T f(T, T) */
#define CASES_F2 \
	F2(d, fdim,  U, -1e6, 1e6, -1e6, 1e6, 0) F2(f, fdimf, U, -1e6, 1e6, -1e6, 1e6, 0) \
	F2(l, fdiml, U, -1e6, 1e6, -1e6, 1e6, 0) \
	F2(d, fmax,  U, -1e6, 1e6, -1e6, 1e6, 0) F2(f, fmaxf, U, -1e6, 1e6, -1e6, 1e6, 0) \
	F2(l, fmaxl, U, -1e6, 1e6, -1e6, 1e6, 0) \
	F2(d, fmin,  U, -1e6, 1e6, -1e6, 1e6, 0) F2(f, fminf, U, -1e6, 1e6, -1e6, 1e6, 0) \
	F2(l, fminl, U, -1e6, 1e6, -1e6, 1e6, 0) \
	F2(d, nextafter, E, -1070, 1023, -1070, 1023, 0) F2(f, nextafterf, E, -149, 127, -149, 127, 0) \
	F2(l, nextafterl, E, -16000, 16000, -16000, 16000, 0) \
	F2(f, remainderf, U, -1e6, 1e6, -100, 100, 0) F2(l, remainderl, U, -1e6, 1e6, -100, 100, 0) \
	F2(f, copysignf, U, -1e6, 1e6, -1, 1, 0) F2(l, copysignl, U, -1e6, 1e6, -1, 1, 0) \
	F2(f, hypotf, E, -60, 60, -60, 60, 1)   F2(l, hypotl, E, -8000, 8000, -8000, 8000, 2) \
	F2(l, atan2l, U, -100, 100, -100, 100, 3) \
	F2(l, powl,   U, 0, 100, -100, 100, 4) \
	F2(l, fmodl,  U, -1e6, 1e6, -100, 100, 0) \
	F2(d, scalb,  U, -1e6, 1e6, -100, 100, 0) F2(f, scalbf, U, -1e6, 1e6, -100, 100, 0)

/* T f(T, T, T) */
#define CASES_F3 \
	F3(d, fma,  U, -1e6, 1e6, -1e6, 1e6, 0) F3(f, fmaf, U, -1e6, 1e6, -1e6, 1e6, 0) \
	F3(l, fmal, U, -1e6, 1e6, -1e6, 1e6, 0)

/* int ilogb(T); long lrint(T) ...; long long llrint(T) ... */
#define CASES_FI \
	FI(d, ilogb, E, -1070, 1023, 0,0, 0) FI(f, ilogbf, E, -149, 127, 0,0, 0) \
	FI(l, ilogbl, E, -16000, 16000, 0,0, 0) \
	FI(d, finite, E, -1070, 1023, 0,0, 0) FI(f, finitef, E, -149, 127, 0,0, 0)
#define CASES_FL \
	FL(d, lrint, U, -2e9, 2e9, 0,0, 0)  FL(f, lrintf, U, -2e9, 2e9, 0,0, 0) \
	FL(l, lrintl, U, -2e9, 2e9, 0,0, 0) \
	FL(d, lround, U, -2e9, 2e9, 0,0, 0) FL(f, lroundf, U, -2e9, 2e9, 0,0, 0) \
	FL(l, lroundl, U, -2e9, 2e9, 0,0, 0) \
	FL(d, lrint, U, -10, 10, 0,0, 0)    FL(d, lround, U, -10, 10, 0,0, 0)
#define CASES_FLL \
	FLL(d, llrint, U, -9e18, 9e18, 0,0, 0)  FLL(f, llrintf, U, -9e18, 9e18, 0,0, 0) \
	FLL(l, llrintl, U, -9e18, 9e18, 0,0, 0) \
	FLL(d, llround, U, -9e18, 9e18, 0,0, 0) FLL(f, llroundf, U, -9e18, 9e18, 0,0, 0) \
	FLL(l, llroundl, U, -9e18, 9e18, 0,0, 0) \
	FLL(d, llrint, U, -100, 100, 0,0, 0)    FLL(l, llroundl, U, -100, 100, 0,0, 0)

/* T f(T, int *): frexp, lgamma_r; T f(T, T, int *): remquo; T f(T, T *): modf */
#define CASES_FPI \
	FPI(f, frexpf, E, -149, 127, 0,0, 0) FPI(l, frexpl, E, -16000, 16000, 0,0, 0) \
	FPI(d, lgamma_r, U, -100.5, 1e6, 0,0, 8) FPI(f, lgammaf_r, U, -30.5, 1e6, 0,0, 8) \
	FPI(l, lgammal_r, U, -100.5, 1e6, 0,0, 8)
#define CASES_FRQ \
	FRQ(d, remquo, U, -1e6, 1e6, -100, 100, 0) FRQ(f, remquof, U, -1e6, 1e6, -100, 100, 0) \
	FRQ(l, remquol, U, -1e6, 1e6, -100, 100, 0)
#define CASES_FMF \
	FMF(l, modfl, U, -1e6, 1e6, 0,0, 0)

/* T f(T, int): ldexp, scalbn; T f(T, long): scalbln; T f(T, long double): nexttoward */
#define CASES_FXI \
	FXI(f, ldexpf, E, -149, 127, -300, 300, 0) FXI(l, ldexpl, E, -16000, 16000, -20000, 20000, 0) \
	FXI(d, scalbn, E, -1070, 1023, -2200, 2200, 0) FXI(f, scalbnf, E, -149, 127, -300, 300, 0) \
	FXI(l, scalbnl, E, -16000, 16000, -20000, 20000, 0)
#define CASES_FXL \
	FXL(d, scalbln, E, -1070, 1023, -2200, 2200, 0) FXL(f, scalblnf, E, -149, 127, -300, 300, 0) \
	FXL(l, scalblnl, E, -16000, 16000, -20000, 20000, 0)
#define CASES_FNT \
	FNT(d, nexttoward, E, -1070, 1023, -1070, 1023, 0) FNT(f, nexttowardf, E, -149, 127, -149, 127, 0) \
	FNT(l, nexttowardl, E, -16000, 16000, -16000, 16000, 0)

/* complex: C f(C); T f(C); C cpow(C, C).  Ranges are for both parts; tol is
 * against the larger part of glibc's result.  Results for an infinite or NaN
 * part (C99 Annex G) are counted, not judged: musl does not follow Annex G
 * in catan, catanh, casin, cacos and their relatives. */
#define CASES_C1 \
	C1(d, cacos,  U, -10, 10, 0,0, 128)  C1(f, cacosf,  U, -10, 10, 0,0, 128)  C1(l, cacosl,  U, -10, 10, 0,0, 262144) \
	C1(d, casin,  U, -10, 10, 0,0, 128)  C1(f, casinf,  U, -10, 10, 0,0, 128)  C1(l, casinl,  U, -10, 10, 0,0, 262144) \
	C1(d, catan,  U, -10, 10, 0,0, 4)  C1(f, catanf,  U, -10, 10, 0,0, 4)  C1(l, catanl,  U, -10, 10, 0,0, 8) \
	C1(d, cacosh, U, -10, 10, 0,0, 128)  C1(f, cacoshf, U, -10, 10, 0,0, 128)  C1(l, cacoshl, U, -10, 10, 0,0, 262144) \
	C1(d, casinh, U, -10, 10, 0,0, 128)  C1(f, casinhf, U, -10, 10, 0,0, 128)  C1(l, casinhl, U, -10, 10, 0,0, 262144) \
	C1(d, catanh, U, -10, 10, 0,0, 4)  C1(f, catanhf, U, -10, 10, 0,0, 4)  C1(l, catanhl, U, -10, 10, 0,0, 8) \
	C1(d, ccos,   U, -10, 10, 0,0, 4)  C1(f, ccosf,   U, -10, 10, 0,0, 4)  C1(l, ccosl,   U, -10, 10, 0,0, 131072) \
	C1(d, csin,   U, -10, 10, 0,0, 4)  C1(f, csinf,   U, -10, 10, 0,0, 4)  C1(l, csinl,   U, -10, 10, 0,0, 131072) \
	C1(d, ctan,   U, -10, 10, 0,0, 4)  C1(f, ctanf,   U, -10, 10, 0,0, 4)  C1(l, ctanl,   U, -10, 10, 0,0, 131072) \
	C1(d, ccosh,  U, -10, 10, 0,0, 4)  C1(f, ccoshf,  U, -10, 10, 0,0, 4)  C1(l, ccoshl,  U, -10, 10, 0,0, 131072) \
	C1(d, csinh,  U, -10, 10, 0,0, 4)  C1(f, csinhf,  U, -10, 10, 0,0, 4)  C1(l, csinhl,  U, -10, 10, 0,0, 131072) \
	C1(d, ctanh,  U, -10, 10, 0,0, 8)  C1(f, ctanhf,  U, -10, 10, 0,0, 8)  C1(l, ctanhl,  U, -10, 10, 0,0, 131072) \
	C1(d, cexp,   U, -10, 10, 0,0, 4)  C1(f, cexpf,   U, -10, 10, 0,0, 4)  C1(l, cexpl,   U, -10, 10, 0,0, 131072) \
	C1(d, clog,   U, -10, 10, 0,0, 4)  C1(f, clogf,   U, -10, 10, 0,0, 4)  C1(l, clogl,   U, -10, 10, 0,0, 131072) \
	C1(d, csqrt,  U, -10, 10, 0,0, 4)  C1(f, csqrtf,  U, -10, 10, 0,0, 4)  C1(l, csqrtl,  U, -10, 10, 0,0, 4096) \
	C1(d, conj,   U, -10, 10, 0,0, 0)  C1(f, conjf,   U, -10, 10, 0,0, 0)  C1(l, conjl,   U, -10, 10, 0,0, 0) \
	C1(d, cproj,  U, -10, 10, 0,0, 0)  C1(f, cprojf,  U, -10, 10, 0,0, 0)  C1(l, cprojl,  U, -10, 10, 0,0, 0)
#define CASES_CR \
	CR(d, cabs,  U, -1e6, 1e6, 0,0, 1) CR(f, cabsf, U, -1e6, 1e6, 0,0, 1) CR(l, cabsl, U, -1e6, 1e6, 0,0, 2) \
	CR(d, carg,  U, -10, 10, 0,0, 2)   CR(f, cargf, U, -10, 10, 0,0, 2)   CR(l, cargl, U, -10, 10, 0,0, 3) \
	CR(d, creal, U, -10, 10, 0,0, 0)   CR(f, crealf, U, -10, 10, 0,0, 0)  CR(l, creall, U, -10, 10, 0,0, 0) \
	CR(d, cimag, U, -10, 10, 0,0, 0)   CR(f, cimagf, U, -10, 10, 0,0, 0)  CR(l, cimagl, U, -10, 10, 0,0, 0)
#define CASES_C2 \
	C2(d, cpow, U, 0.1, 4, -3, 3, 64)  C2(f, cpowf, U, 0.1, 4, -3, 3, 64) C2(l, cpowl, U, 0.1, 4, -3, 3, 32768)
