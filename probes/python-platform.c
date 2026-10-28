/* Focused physical checks for CPython's AMIX compatibility layer. */
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <sys/times.h>
#include <unistd.h>
#include <sys/time.h>
#include <wchar.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
extern int amix_python_monotonic(long long *);

int main(void)
{
    struct tms accounting;
    struct timeval delay;
    clock_t before, after;
    long hz;
    volatile double tiny = 1e-20;
    volatile double negative_zero = -0.0;
    int exponent, failures = 0;
    double got;
    long long monotonic_before, monotonic_after;
    wchar_t buffer[8], *end;
    wchar_t tokens[8], *context, *token;
    hz = sysconf(_SC_CLK_TCK);
    before = times(&accounting);
    if (amix_python_monotonic(&monotonic_before)) return 2;
    delay.tv_sec = 0;
    delay.tv_usec = 100000;
    select(0, NULL, NULL, NULL, &delay);
    after = times(&accounting);
    if (amix_python_monotonic(&monotonic_after)) return 2;
    if (monotonic_after <= monotonic_before) failures++;
    printf("times before=%ld after=%ld ticks-per-second=%ld\n",
           (long)before, (long)after, hz);
    if (hz <= 0 || (unsigned long)(after - before) == 0)
        failures++;
    if (expm1(tiny) != tiny || log1p(tiny) != tiny)
        failures++;
    got = expm1(negative_zero);
    if (!__builtin_signbit(got))
        failures++;
    got = log1p(negative_zero);
    if (!__builtin_signbit(got))
        failures++;
    for (exponent = -100; exponent <= 100; exponent++) {
        volatile double x = ldexp(1.0, exponent);
        if (log2(x) != exponent)
            failures++;
    }
    got = expm1(0.1);
    if (fabs(got - 0.1051709180756476248) > 2e-16)
        failures++;
    got = log1p(0.1);
    if (fabs(got - 0.0953101798043248600) > 2e-16)
        failures++;
    wcscpy(buffer, L"abc");
    if (wcslen(buffer) != 3 || wcscmp(buffer, L"abc")) failures++;
    wcsncpy(buffer, L"xy", 8);
    if (buffer[7] || wcsncmp(buffer, L"xyz", 2)) failures++;
    if (wcschr(buffer, L'y') != buffer + 1 || wcsrchr(buffer, 0) != buffer + 2)
        failures++;
    if (wmemcmp(L"a\0b", L"a\0c", 3) >= 0) failures++;
    if (wcstol(L" -123z", &end, 10) != -123 || *end != L'z') failures++;
    if (wcstol(L"12\u0133", &end, 10) != 12 || *end != 0x133) failures++;
    errno = 0;
    wcstol(L"99999999999999999999999999999999", &end, 10);
    if (errno != ERANGE) failures++;
    wcscpy(tokens, L",a,,b,");
    token = wcstok(tokens, L",", &context);
    if (!token || wcscmp(token, L"a")) failures++;
    token = wcstok(NULL, L",", &context);
    if (!token || wcscmp(token, L"b") || wcstok(NULL, L",", &context)) failures++;
    for (exponent = 0; exponent < 100; exponent++) {
        const char *value = exponent % 2 ? "one" : "two";
        const char *actual;
        if (setenv("AMIX_C_ENVIRONMENT_PROBE", value, 1)) failures++;
        actual = getenv("AMIX_C_ENVIRONMENT_PROBE");
        if (!actual || strcmp(actual, value)) failures++;
    }
    if (setenv("AMIX_C_ENVIRONMENT_PROBE", "unchanged", 0)) failures++;
    if (strcmp(getenv("AMIX_C_ENVIRONMENT_PROBE"), "one")) failures++;
    if (unsetenv("AMIX_C_ENVIRONMENT_PROBE") || getenv("AMIX_C_ENVIRONMENT_PROBE")) failures++;
    if (round(-2.5) != -3.0 || round(2.5) != 3.0) failures++;
    if (exp2(100) != ldexp(1.0, 100)) failures++;
    if (nextafter(0.0, -1.0) >= 0.0 || nextafter(1.0, 2.0) <= 1.0) failures++;
    printf("monotonic interval-ns=%ld wide-character-bytes=%d\n",
           (long)(monotonic_after - monotonic_before), (int)sizeof(wchar_t));
    printf("Python platform probe failures=%d\n", failures);
    return failures != 0;
}
