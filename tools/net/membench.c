/* membench.c -- memory and FPU speed of an AMIX user process (gcc14-cross-amix) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static double now(void) { struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec + tv.tv_usec / 1e6; }

int main(int argc, char **argv)
{
	int kb[] = { 4, 16, 64, 256, 1024 }, i, j, n;
	for (i = 0; i < 5; i++) {
		size_t sz = (size_t)kb[i] * 1024;
		char *a = malloc(sz), *b = malloc(sz);
		double t, mb;
		memset(a, 1, sz); memset(b, 2, sz);
		n = (int)(16 * 1024 * 1024 / sz);
		t = now();
		for (j = 0; j < n; j++) memcpy(b, a, sz);
		t = now() - t; mb = (double)sz * n / 1048576;
		printf("memcpy %5d KB: %6.1f MB/s", kb[i], mb / t);
		t = now();
		for (j = 0; j < n; j++) if (memcmp(b, a, sz)) a[0]++;
		t = now() - t;
		printf("   memcmp %6.1f MB/s", mb / t);
		t = now();
		for (j = 0; j < n; j++) memset(b, j, sz);
		t = now() - t;
		printf("   memset %6.1f MB/s\n", mb / t);
		free(a); free(b);
	}
	{	/* own 32-bit loops, unrolled x4, on 64 KB */
		size_t sz = 65536, k;
		unsigned long *a = malloc(sz), *b = malloc(sz), diff = 0;
		double t, mb;
		memset(a, 1, sz); memset(b, 1, sz);
		n = 256;
		t = now();
		for (j = 0; j < n; j++) {
			unsigned long *s = a, *d = b;
			for (k = sz / 16; k; k--) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; d += 4; s += 4; }
		}
		t = now() - t; mb = (double)sz * n / 1048576;
		printf("long copy  64 KB: %6.1f MB/s", mb / t);
		t = now();
		for (j = 0; j < n; j++) {
			unsigned long *s = a, *d = b;
			for (k = sz / 16; k; k--) { diff |= (d[0] ^ s[0]) | (d[1] ^ s[1]) | (d[2] ^ s[2]) | (d[3] ^ s[3]); d += 4; s += 4; }
		}
		t = now() - t;
		printf("   long compare %6.1f MB/s (%lu)\n", mb / t, diff);
		sz = 4096; n = 4096;
		t = now();
		for (j = 0; j < n; j++) {
			unsigned long *s = a, *d = b;
			for (k = sz / 16; k; k--) { diff |= (d[0] ^ s[0]) | (d[1] ^ s[1]) | (d[2] ^ s[2]) | (d[3] ^ s[3]); d += 4; s += 4; }
		}
		t = now() - t; mb = (double)sz * n / 1048576;
		printf("long compare 4 KB (in cache): %6.1f MB/s (%lu)\n", mb / t, diff);
	}
	{
		volatile double x = 1.000001, y = 0.999999, acc = 0;
		float fx = 1.0001f, facc = 0;
		double t = now();
		for (j = 0; j < 5000000; j++) acc = acc * y + x;
		t = now() - t;
		printf("double mul+add: %.1f M/s (%g)\n", 5.0 / t, (double)acc);
		t = now();
		for (j = 0; j < 5000000; j++) facc = facc * 0.5f + fx;
		t = now() - t;
		printf("float mul+add:  %.1f M/s (%g)\n", 5.0 / t, (double)facc);
		t = now();
		for (j = 0, n = 0; j < 50000000; j++) n += j ^ (n >> 3);
		t = now() - t;
		printf("int loop:       %.1f M iterations/s (%d)\n", 50.0 / t, n);
	}
	(void)argc; (void)argv;
	return 0;
}
