/* fnmatch.h -- AMIX (SVR4.0) has no fnmatch(); a small one for Blender's file
 * selector filters: '*', '?' and '[...]' (with '!' or '^' negation and ranges).
 * Flags are accepted and ignored. */
#ifndef AMIX_FNMATCH_H
#define AMIX_FNMATCH_H
#define FNM_NOMATCH 1
#define FNM_NOESCAPE 0x01
#define FNM_PATHNAME 0x02
#define FNM_PERIOD   0x04
static int fnmatch(const char *p, const char *s, int flags)
{
    (void) flags;
    for (; *p; p++, s++) {
        if (*p == '*') {
            while (p[1] == '*')
                p++;
            if (!p[1])
                return 0;
            for (; *s; s++)
                if (fnmatch(p + 1, s, flags) == 0)
                    return 0;
            return FNM_NOMATCH;
        }
        if (!*s)
            return FNM_NOMATCH;
        if (*p == '[') {
            const char *q = p + 1;
            int neg = (*q == '!' || *q == '^'), hit = 0;
            if (neg)
                q++;
            for (; *q && *q != ']'; q++) {
                if (q[1] == '-' && q[2] && q[2] != ']') {
                    if (*s >= *q && *s <= q[2])
                        hit = 1;
                    q += 2;
                } else if (*q == *s)
                    hit = 1;
            }
            if (hit == neg || !*q)
                return FNM_NOMATCH;
            p = q;
        } else if (*p != '?' && *p != *s)
            return FNM_NOMATCH;
    }
    return *s ? FNM_NOMATCH : 0;
}
#endif
