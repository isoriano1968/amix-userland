/* peekmem.c -- read physical memory through /dev/mem (lseek + read, no mmap).
 *   peekmem ADDR [LEN]     ADDR hex; prints hex longs and ASCII */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	unsigned long a, n, i;
	unsigned char b[256];
	int fd;

	if (argc < 2)
		return 2;
	a = strtoul(argv[1], NULL, 16);
	n = argc > 2 ? strtoul(argv[2], NULL, 0) : 16;
	if (n > sizeof b)
		n = sizeof b;
	fd = open("/dev/mem", O_RDONLY);
	if (fd < 0 || lseek(fd, (long)a, SEEK_SET) < 0 || read(fd, b, n) != (long)n) {
		perror("/dev/mem");
		return 1;
	}
	printf("%08lx:", a);
	for (i = 0; i + 3 < n; i += 4)
		printf(" %02x%02x%02x%02x", b[i], b[i + 1], b[i + 2], b[i + 3]);
	printf("  |");
	for (i = 0; i < n; i++)
		putchar(b[i] >= 0x20 && b[i] < 0x7f ? b[i] : '.');
	printf("|\n");
	return 0;
}
