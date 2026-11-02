/* wchar.h -- the C locale's wide characters for AMIX: compat/amix-gnu.c */
#ifndef AMIX_WCHAR_H
#define AMIX_WCHAR_H
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifndef WEOF
#define WEOF (-1)
#endif
typedef int wint_t;
typedef struct { int unused; } mbstate_t;
typedef unsigned long wctype_t;
size_t wcslen(const wchar_t *);
wchar_t *wcscat(wchar_t *, const wchar_t *);
wchar_t *wmemcpy(wchar_t *, const wchar_t *, size_t);
size_t mbrtowc(wchar_t *, const char *, size_t, mbstate_t *);
size_t wcrtomb(char *, wchar_t, mbstate_t *);
size_t mbrlen(const char *, size_t, mbstate_t *);
int mbsinit(const mbstate_t *);
int wcwidth(wchar_t);
#endif
