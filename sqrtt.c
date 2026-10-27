/* sqrtt: libm060's FSQRT sqrt against a Newton reference */
#include <stdio.h>
double sqrt(double);
float sqrtf(float);
float atanf(float);
int main(void)
{
    double x, worst = 0;
    int bad = 0;
    for (x = 1e-6; x < 1e7; x *= 1.37) {
        double r = sqrt(x), e = (r * r - x) / x;
        if (e < 0) e = -e;
        if (e > worst) worst = e;
        if (e > 1e-14) bad++;
    }
    printf("sqrt: worst rel error %g, %d bad; sqrt(2)=%.17g sqrtf(2)=%.9g atanf(1)=%.9g sqrt(-1)=%g\n",
           worst, bad, sqrt(2.0), (double) sqrtf(2.0f), (double) atanf(1.0f), sqrt(-1.0));
    return 0;
}
