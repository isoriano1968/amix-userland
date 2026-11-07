/*
 * lgamma060.c -- lgamma for lib060 on musl's __lgamma_r, with the sign of
 * gamma(x) in signgam, where AMIX's libm keeps it (SVID).  m060-group.sh
 * adds gamma, _lgamma and _gamma as other names for it: in SVR4 gamma() is
 * lgamma(), and AMIX's gamma.o defines all four (and traps, FMOVECR).
 */
double __lgamma_r(double, int *);

int signgam;

double lgamma(double x)
{
	return __lgamma_r(x, &signgam);
}
