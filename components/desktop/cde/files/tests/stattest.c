/* stattest.c -- does stat() fill struct stat correctly in gcc14-built code?
 * dtpad shows no text and dtfile no files; both read through stat(). */
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stddef.h>

int main(int argc, char **argv)
{
	struct stat st;
	int i, fd;
	printf("sizeof(struct stat)=%d st_size@%d st_mode@%d\n", (int)sizeof st,
	    (int)offsetof(struct stat, st_size), (int)offsetof(struct stat, st_mode));
	for (i = 1; i < argc; i++) {
		int r = stat(argv[i], &st);
		printf("stat(%s)=%d mode=%o size=%ld mtime=%ld\n", argv[i], r,
		    (unsigned)st.st_mode, (long)st.st_size, (long)st.st_mtime);
		fd = open(argv[i], O_RDONLY);
		r = fstat(fd, &st);
		printf("fstat=%d mode=%o size=%ld\n", r, (unsigned)st.st_mode, (long)st.st_size);
		close(fd);
	}
	return 0;
}
