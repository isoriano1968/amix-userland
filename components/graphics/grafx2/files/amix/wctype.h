/* wctype.h for Grafx2 on AMIX: AMIX has no wide characters; Grafx2 only folds
 * the case of its 16-bit Unicode file names, done here for ASCII */
#ifndef AMIX_WCTYPE_H
#define AMIX_WCTYPE_H
#include <ctype.h>
typedef unsigned int wint_t;
#define towlower(c) ((wint_t)(c) < 128 ? (wint_t)tolower((int)(c)) : (wint_t)(c))
#define towupper(c) ((wint_t)(c) < 128 ? (wint_t)toupper((int)(c)) : (wint_t)(c))
#endif
