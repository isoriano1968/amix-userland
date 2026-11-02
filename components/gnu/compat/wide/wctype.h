/* wctype.h -- the C locale's: compat/amix-gnu.c */
#ifndef AMIX_WCTYPE_H
#define AMIX_WCTYPE_H
#include <wchar.h>
#include <ctype.h>
wctype_t wctype(const char *);
int iswctype(wint_t, wctype_t);
#define towlower(c) ((c) < 256 ? tolower(c) : (c))
#define towupper(c) ((c) < 256 ? toupper(c) : (c))
#define iswalnum(c) ((c) < 256 && isalnum(c))
#define iswalpha(c) ((c) < 256 && isalpha(c))
#define iswblank(c) ((c) == ' ' || (c) == '\t')
#define iswcntrl(c) ((c) < 256 && iscntrl(c))
#define iswdigit(c) ((c) < 256 && isdigit(c))
#define iswgraph(c) ((c) < 256 && isgraph(c))
#define iswlower(c) ((c) < 256 && islower(c))
#define iswprint(c) ((c) < 256 && isprint(c))
#define iswpunct(c) ((c) < 256 && ispunct(c))
#define iswspace(c) ((c) < 256 && isspace(c))
#define iswupper(c) ((c) < 256 && isupper(c))
#define iswxdigit(c) ((c) < 256 && isxdigit(c))
#endif
