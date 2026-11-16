/* hash.c -- SHA-1 (file names, catalog signatures) and SHA-512/256
 * (pkg.content-hash), FIPS 180-4, without OpenSSL. */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "pkg.h"

static void
hex(const uint8_t *d, size_t n, char *out)
{
	static const char x[] = "0123456789abcdef";
	size_t i;

	for (i = 0; i < n; i++) {
		out[2 * i] = x[d[i] >> 4];
		out[2 * i + 1] = x[d[i] & 15];
	}
	out[2 * n] = 0;
}

/* --- SHA-1 ---------------------------------------------------------------- */

#define ROL32(x, n)	(((x) << (n)) | ((x) >> (32 - (n))))

static void
sha1_block(uint32_t h[5], const uint8_t *p)
{
	uint32_t w[80], a, b, c, d, e, t;
	int i;

	for (i = 0; i < 16; i++)
		w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 |
		    (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
	for (; i < 80; i++)
		w[i] = ROL32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];
	for (i = 0; i < 80; i++) {
		uint32_t f, k;

		if (i < 20) {
			f = (b & c) | (~b & d);
			k = 0x5a827999;
		} else if (i < 40) {
			f = b ^ c ^ d;
			k = 0x6ed9eba1;
		} else if (i < 60) {
			f = (b & c) | (b & d) | (c & d);
			k = 0x8f1bbcdc;
		} else {
			f = b ^ c ^ d;
			k = 0xca62c1d6;
		}
		t = ROL32(a, 5) + f + e + k + w[i];
		e = d; d = c; c = ROL32(b, 30); b = a; a = t;
	}
	h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

/* --- one interface for both: hash_init, hash_update, hash_final ---------- */

static void
sha1_init(struct hash *x)
{
	static const uint32_t iv[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 };

	memcpy(x->h1, iv, sizeof(iv));
}

static void
sha1_final(struct hash *x, char out[41])
{
	uint8_t d[20];
	uint64_t bits = x->total * 8;
	size_t i, nb;

	x->blk[x->fill] = 0x80;
	memset(x->blk + x->fill + 1, 0, sizeof(x->blk) - x->fill - 1);
	nb = x->fill + 9 > 64 ? 128 : 64;
	for (i = 0; i < 8; i++)
		x->blk[nb - 1 - i] = (uint8_t)(bits >> (8 * i));
	sha1_block(x->h1, x->blk);
	if (nb == 128)
		sha1_block(x->h1, x->blk + 64);
	for (i = 0; i < 5; i++) {
		d[4 * i] = (uint8_t)(x->h1[i] >> 24);
		d[4 * i + 1] = (uint8_t)(x->h1[i] >> 16);
		d[4 * i + 2] = (uint8_t)(x->h1[i] >> 8);
		d[4 * i + 3] = (uint8_t)x->h1[i];
	}
	hex(d, 20, out);
}

/* --- SHA-512/256 ---------------------------------------------------------- */

static const uint64_t K512[80] = {
	0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
	0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
	0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
	0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
	0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
	0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
	0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
	0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
	0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
	0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
	0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
	0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
	0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
	0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
	0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
	0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
	0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
	0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
	0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
	0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

#define ROR64(x, n)	(((x) >> (n)) | ((x) << (64 - (n))))

static void
sha512_block(uint64_t h[8], const uint8_t *p)
{
	uint64_t w[80], a, b, c, d, e, f, g, hh, t1, t2;
	int i, j;

	for (i = 0; i < 16; i++) {
		w[i] = 0;
		for (j = 0; j < 8; j++)
			w[i] = w[i] << 8 | p[8 * i + j];
	}
	for (; i < 80; i++) {
		uint64_t s0 = ROR64(w[i - 15], 1) ^ ROR64(w[i - 15], 8) ^ (w[i - 15] >> 7);
		uint64_t s1 = ROR64(w[i - 2], 19) ^ ROR64(w[i - 2], 61) ^ (w[i - 2] >> 6);

		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; hh = h[7];
	for (i = 0; i < 80; i++) {
		t1 = hh + (ROR64(e, 14) ^ ROR64(e, 18) ^ ROR64(e, 41)) + ((e & f) ^ (~e & g)) +
		    K512[i] + w[i];
		t2 = (ROR64(a, 28) ^ ROR64(a, 34) ^ ROR64(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
		hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
	}
	h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

static void
sha512_init(struct hash *x)
{
	/* the SHA-512/256 initial values (FIPS 180-4, 5.3.6.2) */
	static const uint64_t iv[8] = {
		0x22312194fc2bf72cULL, 0x9f555fa3c84c64c2ULL, 0x2393b86b6f53b151ULL,
		0x963877195940eabdULL, 0x96283ee2a88effe3ULL, 0xbe5e1e2553863992ULL,
		0x2b0199fc2c85b8aaULL, 0x0eb72ddc81c52ca2ULL,
	};

	memcpy(x->h5, iv, sizeof(iv));
}

static void
sha512_final(struct hash *x, char out[65])
{
	uint8_t last[256], d[32];
	uint64_t bits = x->total * 8;
	size_t i, nb;

	memset(last, 0, sizeof(last));
	memcpy(last, x->blk, x->fill);
	last[x->fill] = 0x80;
	nb = x->fill + 17 > 128 ? 256 : 128;	/* 128-bit length; the high 64 bits stay 0 */
	for (i = 0; i < 8; i++)
		last[nb - 1 - i] = (uint8_t)(bits >> (8 * i));
	sha512_block(x->h5, last);
	if (nb == 256)
		sha512_block(x->h5, last + 128);
	for (i = 0; i < 32; i++)
		d[i] = (uint8_t)(x->h5[i / 8] >> (56 - 8 * (i % 8)));
	hex(d, 32, out);
}

void
hash_init(struct hash *x, int kind)
{
	memset(x, 0, sizeof(*x));
	x->kind = kind;
	if (kind == HASH_SHA1)
		sha1_init(x);
	else
		sha512_init(x);
}

void
hash_update(struct hash *x, const void *data, size_t n)
{
	const uint8_t *p = data;
	size_t bs = x->kind == HASH_SHA1 ? 64 : 128;

	x->total += n;
	while (n) {
		size_t take = bs - x->fill;

		if (take > n)
			take = n;
		if (x->fill == 0 && n >= bs) {		/* whole blocks straight from p */
			take = bs;
			if (x->kind == HASH_SHA1)
				sha1_block(x->h1, p);
			else
				sha512_block(x->h5, p);
		} else {
			memcpy(x->blk + x->fill, p, take);
			x->fill += take;
			if (x->fill == bs) {
				if (x->kind == HASH_SHA1)
					sha1_block(x->h1, x->blk);
				else
					sha512_block(x->h5, x->blk);
				x->fill = 0;
			}
		}
		p += take;
		n -= take;
	}
}

void
hash_final(struct hash *x, char *out)
{
	if (x->kind == HASH_SHA1)
		sha1_final(x, out);
	else
		sha512_final(x, out);
}

void
sha1_hex(const void *data, size_t n, char out[41])
{
	struct hash x;

	hash_init(&x, HASH_SHA1);
	hash_update(&x, data, n);
	hash_final(&x, out);
}

void
sha512_256_hex(const void *data, size_t n, char out[65])
{
	struct hash x;

	hash_init(&x, HASH_SHA512_256);
	hash_update(&x, data, n);
	hash_final(&x, out);
}

/* the hash of a file, read in pieces: 0, or -1 with errno */
int
hash_file(const char *path, int kind, char *out)
{
	struct hash x;
	char tmp[16384];
	ssize_t n;
	int fd = open(path, O_RDONLY);

	if (fd < 0)
		return -1;
	hash_init(&x, kind);
	while ((n = read(fd, tmp, sizeof(tmp))) > 0)
		hash_update(&x, tmp, (size_t)n);
	if (n < 0) {
		int e = errno;

		close(fd);
		errno = e;
		return -1;
	}
	close(fd);
	hash_final(&x, out);
	return 0;
}
