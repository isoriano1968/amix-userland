/* amix-pygame.h -- forced into every pygame-ce file: what AMIX's SVR4.0 libc
 * and libm lack, as static inline functions so that each module stays
 * self-contained (nothing of this is exported or taken from python3). */
#ifndef AMIX_PYGAME_H
#define AMIX_PYGAME_H
#include <stddef.h>
#include <string.h>
#include <math.h>
/* in libc, declared only for __STDC__ == 0 */
extern char *strdup(const char *);
/* POSIX strtok_r: not in SVR4.0 */
static inline char *amix_strtok_r(char *s, const char *sep, char **save)
{
	char *e;
	if (s == NULL) s = *save;
	s += strspn(s, sep);
	if (*s == '\0') { *save = s; return NULL; }
	e = s + strcspn(s, sep);
	if (*e != '\0') *e++ = '\0';
	*save = e;
	return s;
}
#define strtok_r(s, sep, save) amix_strtok_r(s, sep, save)
/* C99 rounding: halves away from zero, as round() */
static inline float amix_roundf(float x) { return x < 0 ? -(float)(long)(0.5f - x) : (float)(long)(x + 0.5f); }
static inline double amix_round(double x) { return x < 0 ? -(double)(long)(0.5 - x) : (double)(long)(x + 0.5); }
/* lrint in the default rounding mode (to nearest, ties to even) */
static inline long amix_lrint(double x)
{
	double f = floor(x), d = x - f;
	long r = (long)f;
	if (d > 0.5 || (d == 0.5 && (r & 1))) r++;
	return r;
}
#define roundf(x) amix_roundf(x)
#define round(x) amix_round(x)
#define lrint(x) amix_lrint(x)
/* no snprintf in SVR4.0's libc: SDL's, which every module links anyway */
#include <stdarg.h>
extern int SDL_snprintf(char *, size_t, const char *, ...);
extern int SDL_vsnprintf(char *, size_t, const char *, va_list);
#define snprintf SDL_snprintf
#define vsnprintf SDL_vsnprintf
/* no float variants in AMIX's libm */
#define fmodf(x, y) ((float)fmod((double)(x), (double)(y)))
#endif
