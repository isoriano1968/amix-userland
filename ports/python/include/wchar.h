#ifndef AMIX_PYTHON_WCHAR_H
#define AMIX_PYTHON_WCHAR_H
/* Private wide-string subset for the CPython port; AMIX defines wchar_t in
 * stddef.h/stdlib.h but has no wchar.h. Conversion stays with libc/CPython. */
#include <stddef.h>
#include <stdlib.h>
typedef unsigned int wint_t;
#define WEOF ((wint_t)-1)
#ifndef WCHAR_MIN
#define WCHAR_MIN (-__WCHAR_MAX__ - 1L)
#endif
#ifndef WCHAR_MAX
#define WCHAR_MAX __WCHAR_MAX__
#endif
size_t wcslen(const wchar_t *);
int wcscmp(const wchar_t *, const wchar_t *);
int wcsncmp(const wchar_t *, const wchar_t *, size_t);
wchar_t *wcscpy(wchar_t *, const wchar_t *);
wchar_t *wcsncpy(wchar_t *, const wchar_t *, size_t);
wchar_t *wcschr(const wchar_t *, wchar_t);
wchar_t *wcsrchr(const wchar_t *, wchar_t);
int wmemcmp(const wchar_t *, const wchar_t *, size_t);
long wcstol(const wchar_t *, wchar_t **, int);
wchar_t *wcstok(wchar_t *, const wchar_t *, wchar_t **);
#endif
