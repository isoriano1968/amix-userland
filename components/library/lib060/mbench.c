/* mbench.c -- microseconds per call of AMIX libm (and libm060, when linked
   first) on the 060: which functions still trap or are slow. */
#include <stdio.h>
#include <math.h>
#include <sys/time.h>
/* not ANSI, so hidden by AMIX math.h in strict mode */
double hypot(double, double), erf(double), erfc(double), lgamma(double);
double j0(double), j1(double), y0(double), y1(double), jn(int, double);

static volatile double sink;

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

#define N 20000
#define BENCH(name, expr) do { \
    double t0 = now(), x = 0.1; int i; \
    for (i = 0; i < N; i++) { sink = (expr); x += 0.00004; } \
    printf("%-8s %7.2f us\n", name, (now() - t0) * 1e6 / N); \
} while (0)

int main(void)
{
    BENCH("empty", x);
    BENCH("sqrt", sqrt(x));
    BENCH("sin", sin(x));
    BENCH("cos", cos(x));
    BENCH("tan", tan(x));
    BENCH("atan", atan(x));
    BENCH("atan2", atan2(x, 0.7));
    BENCH("asin", asin(x));
    BENCH("acos", acos(x));
    BENCH("exp", exp(x));
    BENCH("log", log(x + 1.0));
    BENCH("pow", pow(x + 1.0, 1.7));
    BENCH("floor", floor(x * 100.0));
    BENCH("ceil", ceil(x * 100.0));
    BENCH("fabs", fabs(x - 0.5));
    BENCH("fmod", fmod(x * 100.0, 3.0));
    BENCH("hypot", hypot(x, 0.7));
    BENCH("erf", erf(x));
    BENCH("erfc", erfc(x));
    BENCH("lgamma", lgamma(x + 1.0));
    BENCH("j0", j0(x * 10));
    BENCH("j1", j1(x * 10));
    BENCH("y0", y0(x * 10));
    BENCH("y1", y1(x * 10));
    BENCH("jn(3)", jn(3, x * 10));
    BENCH("atanf", atanf((float) x));
    BENCH("atan2f", atan2f((float) x, 0.7f));
    BENCH("sqrtf", sqrtf((float) x));
    BENCH("floorf", floorf((float) x * 100.0f));
    return 0;
}
