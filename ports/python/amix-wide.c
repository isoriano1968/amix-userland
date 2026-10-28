/* Private, code-point based string operations for CPython on AMIX. */
#include <wchar.h>
#include <stdlib.h>
#include <errno.h>

size_t wcslen(const wchar_t *s)
{
    const wchar_t *end = s;
    while (*end) end++;
    return end - s;
}
int wcscmp(const wchar_t *a, const wchar_t *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (*a > *b) - (*a < *b);
}
int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    while (n) {
        if (*a != *b) return (*a > *b) - (*a < *b);
        if (!*a) return 0;
        a++; b++; n--;
    }
    return 0;
}
wchar_t *wcscpy(wchar_t *to, const wchar_t *from)
{
    wchar_t *result = to;
    while ((*to++ = *from++) != 0) {}
    return result;
}
wchar_t *wcsncpy(wchar_t *to, const wchar_t *from, size_t n)
{
    wchar_t *result = to;
    while (n && *from) { *to++ = *from++; n--; }
    while (n) { *to++ = 0; n--; }
    return result;
}
wchar_t *wcschr(const wchar_t *s, wchar_t c)
{
    do { if (*s == c) return (wchar_t *)s; } while (*s++);
    return NULL;
}
wchar_t *wcsrchr(const wchar_t *s, wchar_t c)
{
    const wchar_t *last = NULL;
    do { if (*s == c) last = s; } while (*s++);
    return (wchar_t *)last;
}
int wmemcmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    while (n) {
        if (*a != *b) return (*a > *b) - (*a < *b);
        a++; b++; n--;
    }
    return 0;
}
wchar_t *wcstok(wchar_t *s, const wchar_t *delimiters, wchar_t **context)
{
    wchar_t *token;
    if (!s) s = *context;
    if (!s) return NULL;
    while (*s && wcschr(delimiters, *s)) s++;
    if (!*s) { *context = NULL; return NULL; }
    token = s;
    while (*s && !wcschr(delimiters, *s)) s++;
    if (*s) { *s++ = 0; *context = s; }
    else *context = NULL;
    return token;
}
long wcstol(const wchar_t *s, wchar_t **end, int base)
{
    size_t n = 0, i;
    char *ascii, *stopped;
    long result;
    /* Only the C-locale numeric syntax is needed here. A non-ASCII code point
     * terminates the candidate instead of being truncated to an ASCII digit. */
    while (s[n] && s[n] > 0 && s[n] < 128) n++;
    ascii = malloc(n + 1);
    if (!ascii) {
        errno = ENOMEM;
        if (end) *end = (wchar_t *)s;
        return 0;
    }
    for (i = 0; i < n; i++) ascii[i] = (char)s[i];
    ascii[n] = 0;
    result = strtol(ascii, &stopped, base);
    if (end) *end = (wchar_t *)s + (stopped - ascii);
    free(ascii);
    return result;
}
