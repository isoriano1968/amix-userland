/* shmstrip.c -- can a process attach N shared memory segments of SIZE bytes
   back to back, so that they form one buffer?  (AMIX SHMMAX is 327680.)
     shmstrip [n [size]] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

int main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 14, size = argc > 2 ? atoi(argv[2]) : 307200;
	int id[64], i, got = 0;
	char *base = 0, *p;

	for (i = 0; i < n; i++) {
		id[i] = shmget(IPC_PRIVATE, size, IPC_CREAT | 0600);
		if (id[i] < 0) {
			perror("shmget");
			break;
		}
		p = shmat(id[i], i ? base + (long)i * size : 0, 0);
		if (p == (char *)-1) {
			perror("shmat");
			shmctl(id[i], IPC_RMID, 0);
			break;
		}
		if (!i)
			base = p;
		printf("%2d: id %d at %p\n", i, id[i], p);
		got++;
		shmctl(id[i], IPC_RMID, 0);
	}
	if (got) {
		memset(base, 0x5a, (long)got * size);
		printf("%d segments, %ld bytes from %p written\n", got, (long)got * size, base);
	}
	for (i = 0; i < got; i++)
		shmdt(base + (long)i * size);
	return 0;
}
