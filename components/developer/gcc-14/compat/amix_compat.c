/* amix_compat.c -- snprintf, vsnprintf and usleep for AMIX SVR4.0.
 *
 * WHY NOT A HAND-WRITTEN FORMATTER
 * --------------------------------
 * AMIX's libc has a complete, correct formatter already. What it lacks is a way
 * to point one at a buffer with a length limit, so this borrows the real
 * formatter one conversion at a time rather than reimplementing it: every
 * conversion AMIX supports keeps working, floating point included, and there is
 * no second implementation to get subtly wrong.
 *
 * EXCEPT FOR ONE THING: THE ll MODIFIER
 * -------------------------------------
 * Measured on the machine (AMIX 2.1c), libc's formatter does not understand
 * C99's "ll":
 *     snprintf(b, n, "%lld", 1234567890123LL)   ->  "287"
 * This cannot be worked around at the call site. A long long occupies eight
 * bytes of the argument list whatever conversion you write, so printing it as
 * "%ld" does not merely truncate the value -- it leaves the formatter four
 * bytes out of step and every later argument comes out as garbage. So the ll
 * conversions are the one case rendered here, by hand.
 *
 * That matters for OpenTTD specifically: money is int64, and its stdafx.h
 * routes 64-bit output through OTTD_PRINTF64 / OTTD_PRINTFHEX64, both of which
 * expand to ll forms.
 *
 * WHY NO /dev/null MEASURING PASS ANY MORE
 * ----------------------------------------
 * An earlier version formatted the whole string into /dev/null first, purely to
 * learn its length from vfprintf's return value, and then formatted it again
 * for real. Working one conversion at a time removes the need: the length of a
 * single conversion is boundable from its own width and precision, so a
 * correctly sized buffer can be chosen up front. It also drops a dependency on
 * /dev/null being present, and halves the formatting work.
 *
 * va_list IS COPIED WITH __builtin_va_copy where a copy is needed. C89 -- all
 * AMIX's <stdarg.h> knows -- has no va_copy, and GCC provides the builtin
 * regardless of the C library. Never assign one va_list to another instead: it
 * happens to work on m68k, where a va_list is a plain pointer, and is exactly
 * the kind of assumption that breaks silently on the next target.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>

#include "amix_compat.h"

/* AMIX's <sys/select.h> defines fd_set and the FD_* macros and stops there --
 * the same kind of gap as the missing gettimeofday prototype in <sys/time.h>.
 * libc exports the symbol; only the declaration is absent. */
#ifndef AMIX_COMPAT_HOST_TEST
extern int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
#endif

/* Output shorter than this needs no allocation. One conversion at a time, so
 * this covers everything except deliberately enormous field widths. */
#define AMIX_CONV_STACK 512

/* ---------------------------------------------------------------- the sink --
 * Accumulates output, tracking the length the result WOULD have had. That is
 * what C99 snprintf returns, and getting it wrong is the classic bug: callers
 * use the return value to decide how much to allocate. */
struct sink {
	char *dst;
	size_t size;
	size_t len;      /* untruncated length so far */
};

static void sink_put(struct sink *s, const char *p, size_t n)
{
	if (s->dst != NULL && s->size > 0 && s->len < s->size - 1) {
		size_t room = s->size - 1 - s->len;
		size_t copy = (n < room) ? n : room;
		memcpy(s->dst + s->len, p, copy);
	}
	s->len += n;
}

/* -------------------------------------------------------- the ll renderer --
 * Only the integer conversions can carry ll, so this handles bases 8, 10 and
 * 16 with the flags, width and precision that C99 defines for them. */
static void render_ll(struct sink *s, unsigned long long uv, int negative,
                      int base, int upper, int width, int prec,
                      int leftalign, int zeropad, int plusflag, int spaceflag,
                      int altflag)
{
	char digits[24];                 /* 64 bits in octal needs 22 */
	const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	char prefix[3];
	int ndigits = 0, nprefix = 0, pad;

	if (uv == 0 && prec == 0) {
		/* C99: a precision of zero and a value of zero print nothing. */
		ndigits = 0;
	} else {
		do {
			digits[ndigits++] = set[uv % (unsigned long long)base];
			uv /= (unsigned long long)base;
		} while (uv != 0);
	}
	while (prec > ndigits && ndigits < (int)sizeof(digits)) digits[ndigits++] = '0';

	if (negative)      prefix[nprefix++] = '-';
	else if (plusflag) prefix[nprefix++] = '+';
	else if (spaceflag) prefix[nprefix++] = ' ';
	if (altflag && base == 16 && ndigits > 0) {
		prefix[nprefix++] = '0';
		prefix[nprefix++] = upper ? 'X' : 'x';
	} else if (altflag && base == 8) {
		prefix[nprefix++] = '0';
	}

	pad = width - nprefix - ndigits;
	if (pad < 0) pad = 0;

	/* Zero padding goes after the sign and inside the field; it is ignored
	 * when the value is left-aligned or an explicit precision was given. */
	if (!leftalign && zeropad && prec < 0) {
		if (nprefix > 0) sink_put(s, prefix, (size_t)nprefix);
		while (pad-- > 0) sink_put(s, "0", 1);
		nprefix = 0;
	} else if (!leftalign) {
		while (pad-- > 0) sink_put(s, " ", 1);
	}
	if (nprefix > 0) sink_put(s, prefix, (size_t)nprefix);
	while (ndigits > 0) sink_put(s, &digits[--ndigits], 1);
	if (leftalign) while (pad-- > 0) sink_put(s, " ", 1);
}

/* ------------------------------------------------- one borrowed conversion --
 * Formats a single conversion with libc's own sprintf, into a buffer chosen
 * large enough that sprintf cannot overrun it. */
static void borrow_conv(struct sink *s, const char *spec, size_t need,
                        int class_, long ival, unsigned long uval,
                        double dval, const void *pval)
{
	char stackbuf[AMIX_CONV_STACK];
	char *buf = stackbuf;
	int n;

	if (need >= sizeof(stackbuf)) {
		buf = (char *)malloc(need + 1);
		if (buf == NULL) return;   /* drop the field rather than overrun */
	}
	switch (class_) {
		case 0:  n = sprintf(buf, spec, ival); break;
		case 1:  n = sprintf(buf, spec, uval); break;
		case 2:  n = sprintf(buf, spec, dval); break;
		default: n = sprintf(buf, spec, pval); break;
	}
	if (n > 0) sink_put(s, buf, (size_t)n);
	if (buf != stackbuf) free(buf);
}

int amix_vsnprintf(char *str, size_t size, const char *fmt, va_list ap)
{
	struct sink s;
	const char *p = fmt;

	s.dst = str; s.size = size; s.len = 0;

	while (*p != '\0') {
		char spec[40];
		size_t sl;
		int leftalign, zeropad, plusflag, spaceflag, altflag;
		int width, prec, is_ll, is_long, conv;
		size_t need;

		if (*p != '%') {
			const char *run = p;
			while (*p != '\0' && *p != '%') p++;
			sink_put(&s, run, (size_t)(p - run));
			continue;
		}
		p++;
		if (*p == '%') { sink_put(&s, "%", 1); p++; continue; }

		sl = 0; spec[sl++] = '%';
		leftalign = zeropad = plusflag = spaceflag = altflag = 0;
		for (;;) {
			if (*p == '-')      leftalign = 1;
			else if (*p == '0') zeropad = 1;
			else if (*p == '+') plusflag = 1;
			else if (*p == ' ') spaceflag = 1;
			else if (*p == '#') altflag = 1;
			else break;
			spec[sl++] = *p++;
		}

		width = 0;
		if (*p == '*') {
			width = va_arg(ap, int);
			p++;
			if (width < 0) { leftalign = 1; width = -width; spec[sl++] = '-'; }
			sl += (size_t)sprintf(spec + sl, "%d", width);
		} else {
			while (*p >= '0' && *p <= '9') {
				width = width * 10 + (*p - '0');
				spec[sl++] = *p++;
			}
		}

		prec = -1;
		if (*p == '.') {
			spec[sl++] = *p++;
			prec = 0;
			if (*p == '*') {
				prec = va_arg(ap, int);
				p++;
				if (prec < 0) { prec = -1; sl--; }      /* negative means none */
				else sl += (size_t)sprintf(spec + sl, "%d", prec);
			} else {
				while (*p >= '0' && *p <= '9') {
					prec = prec * 10 + (*p - '0');
					spec[sl++] = *p++;
				}
			}
		}

		/* Length modifiers. Everything here is a 32-bit machine, so h, hh, z,
		 * j and t all promote to int/long and need no mark in the spec; only
		 * l and L do, and ll is handled by us rather than by libc. */
		is_ll = is_long = 0;
		if (p[0] == 'l' && p[1] == 'l')      { is_ll = 1; p += 2; }
		else if (p[0] == 'l')                { is_long = 1; spec[sl++] = *p++; }
		else if (p[0] == 'h' && p[1] == 'h') { p += 2; }
		else if (p[0] == 'h')                { p++; }
		else if (p[0] == 'L')                { spec[sl++] = *p++; }
		else if (p[0] == 'z' || p[0] == 'j' || p[0] == 't') { p++; }

		conv = *p;
		if (conv == '\0') break;             /* trailing '%' with no conversion */
		p++;
		spec[sl++] = (char)conv;
		spec[sl] = '\0';

		need = (size_t)((width > prec ? width : prec) + 64);

		switch (conv) {
			case 'd': case 'i':
				if (is_ll) {
					long long v = va_arg(ap, long long);
					unsigned long long uv = (v < 0)
					    ? (unsigned long long)(-(v + 1)) + 1ULL   /* no overflow at LLONG_MIN */
					    : (unsigned long long)v;
					render_ll(&s, uv, v < 0, 10, 0, width, prec,
					          leftalign, zeropad, plusflag, spaceflag, altflag);
				} else {
					long v = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
					borrow_conv(&s, spec, need, 0, v, 0, 0.0, NULL);
				}
				break;
			case 'u': case 'o': case 'x': case 'X':
				if (is_ll) {
					unsigned long long uv = va_arg(ap, unsigned long long);
					int base = (conv == 'u') ? 10 : (conv == 'o') ? 8 : 16;
					render_ll(&s, uv, 0, base, conv == 'X', width, prec,
					          leftalign, zeropad, plusflag, spaceflag, altflag);
				} else {
					unsigned long v = is_long ? va_arg(ap, unsigned long)
					                          : (unsigned long)va_arg(ap, unsigned int);
					borrow_conv(&s, spec, need, 1, 0, v, 0.0, NULL);
				}
				break;
			case 'c': {
				long v = (long)va_arg(ap, int);
				borrow_conv(&s, spec, need, 0, v, 0, 0.0, NULL);
				break;
			}
			case 's': {
				const char *v = va_arg(ap, const char *);
				size_t vlen;
				if (v == NULL) v = "(null)";
				vlen = strlen(v);
				if (prec >= 0 && (size_t)prec < vlen) vlen = (size_t)prec;
				need = (size_t)width > vlen ? (size_t)width : vlen;
				borrow_conv(&s, spec, need + 8, 3, 0, 0, 0.0, v);
				break;
			}
			case 'p': {
				const void *v = va_arg(ap, const void *);
				borrow_conv(&s, spec, need, 3, 0, 0, 0.0, v);
				break;
			}
			case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
			case 'a': case 'A': {
				double v = va_arg(ap, double);
				/* %f of DBL_MAX is 309 digits before the point, plus the
				 * precision after it. */
				need = (size_t)(width + (prec < 0 ? 6 : prec) + 400);
				borrow_conv(&s, spec, need, 2, 0, 0, v, NULL);
				break;
			}
			case 'n': {
				/* Not supported on purpose: nothing in this port uses it, and
				 * it is the one conversion that writes through a caller
				 * pointer. Consume the argument so the rest stays in step. */
				(void)va_arg(ap, int *);
				break;
			}
			default:
				/* Unknown conversion: emit it literally, consuming nothing.
				 * Silently swallowing it would hide the typo. */
				sink_put(&s, spec, sl);
				break;
		}
	}

	if (str != NULL && size > 0) {
		str[s.len < size ? s.len : size - 1] = '\0';
	}
	return (int)s.len;
}

/* The standard names.  AMIX's libc has neither, so there is nothing to clash
 * with, and portable code that calls snprintf simply works. */
int snprintf(char *str, size_t size, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = amix_vsnprintf(str, size, fmt, ap);
	va_end(ap);
	return n;
}

int vsnprintf(char *str, size_t size, const char *fmt, va_list ap)
{
	return amix_vsnprintf(str, size, fmt, ap);
}

int amix_snprintf(char *str, size_t size, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = amix_vsnprintf(str, size, fmt, ap);
	va_end(ap);
	return n;
}

#ifndef AMIX_COMPAT_HOST_TEST
/* The standard name.  AMIX has sleep() only, in whole seconds, so nothing
 * clashes and portable code that calls usleep simply works. */
int usleep(unsigned long usec)
{
	return amix_usleep(usec);
}

/* select() with no descriptors is the SVR4 way to sleep with sub-second
 * resolution; poll() would round to milliseconds. */
int amix_usleep(unsigned long usec)
{
	struct timeval tv;

	tv.tv_sec = (long)(usec / 1000000UL);
	tv.tv_usec = (long)(usec % 1000000UL);
	return select(0, (fd_set *)0, (fd_set *)0, (fd_set *)0, &tv);
}
#endif /* !AMIX_COMPAT_HOST_TEST */
