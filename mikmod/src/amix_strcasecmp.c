/* strcasecmp() for AMIX -- not declared/provided by its SVR4 libc (see
   usr/local/src/mikmod/config.h). libmikmod has the same gap and works
   around it with its own internal _mm_strcasecmp() (posix/strcasecmp.c in
   the libmikmod tree), but that name isn't exported for other programs to
   link against, and this player calls plain strcasecmp() directly
   (mlistedit.c, mlist.c, mconfig.c, rcfile.c) -- so it needs its own
   linkable copy under the real name. */

int strcasecmp(const char *s1, const char *s2)
{
	unsigned char c1, c2;

	for (;;) {
		c1 = *s1++;
		c2 = *s2++;
		if (c1 >= 'A' && c1 <= 'Z')
			c1 += 'a' - 'A';
		if (c2 >= 'A' && c2 <= 'Z')
			c2 += 'a' - 'A';
		if (c1 != c2)
			return (int)c1 - (int)c2;
		if (c1 == '\0')
			return 0;
	}
}
