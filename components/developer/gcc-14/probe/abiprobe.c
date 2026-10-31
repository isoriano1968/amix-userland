/* abiprobe.c -- the struct-layout rule this target actually uses.
 *
 * m68k GCC rounds a struct up to 2 bytes; AMIX's cc rounds to 4.  For most
 * structs the two agree.  For libc's FILE they do not, and FILE is the one
 * struct where being two bytes out is fatal -- see docs/TOOLCHAIN.md.
 *
 * Every number here was measured with AMIX's own cc, so this file is both the
 * record of the rule and the way to check a compiler against it.  Build it
 * with the cross toolchain and the printed values must match the comments; if
 * they do not, the ABI has drifted and stdio will fail in a way that looks
 * like anything but an alignment problem.
 *
 * The last three lines are the ones that distinguish the real rule from a
 * plausible wrong one: an all-char struct is NOT rounded up, so the rule is
 * "align each member naturally, round the struct to its widest member's
 * alignment" -- not a blanket round-to-4, which would give the same answer for
 * `mixed` for the wrong reason.
 */
#include <stdio.h>
#include <stddef.h>

struct mixed     { char a; short b; char c; int d; };
struct twoptr    { char a; char *p; };
struct justchars { char a, b, c; };

int main(void)
{
	printf("FILE=%d      (AMIX cc: 16   GCC without -malign-int: 14)\n",
			(int)sizeof(FILE));
	printf("_NFILE=%d    iob_bytes=%d  stride=%d\n",
			_NFILE, (int)(_NFILE * (int)sizeof(FILE)), (int)sizeof(FILE));
	printf("stride measured: stdout-stdin=%d stderr-stdout=%d  (AMIX cc: 16 16)\n",
			(int)((char *)stdout - (char *)stdin),
			(int)((char *)stderr - (char *)stdout));

	printf("mixed=%d off_d=%d           (AMIX cc: 12 8)\n",
			(int)sizeof(struct mixed), (int)offsetof(struct mixed, d));
	printf("twoptr=%d off_p=%d          (AMIX cc: 8 4)\n",
			(int)sizeof(struct twoptr), (int)offsetof(struct twoptr, p));
	printf("justchars=%d               (AMIX cc: 3 -- NOT rounded to 4)\n",
			(int)sizeof(struct justchars));
	return 0;
}
