/* hello.c -- the smallest question worth asking of a new toolchain:
 * does an executable built by a MODERN m68k GCC load and run under the AMIX
 * 1991 runtime linker at all?  If this prints, the ELF shape, the PLT format,
 * the crt startup and the libc.so.1 binding are all compatible. */
#include <stdio.h>

int main(int argc, char **argv)
{
	printf("hello from a modern m68k gcc\n");
	printf("  argc = %d, argv[0] = %s\n", argc, argc > 0 ? argv[0] : "(none)");
	return 0;
}
