/* unit.c -- tests of the parts without the command line: hashes (FIPS test
 * vectors), JSON, actions, versions. */
#include <stdlib.h>
#include <string.h>

#include "pkg.h"

static int fails;

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void
hashes(void)
{
	char h[65];
	char *big = xmalloc(1000000);
	const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	const char *two512 = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

	sha1_hex("abc", 3, h);
	CHECK(!strcmp(h, "a9993e364706816aba3e25717850c26c9cd0d89d"));
	sha1_hex("", 0, h);
	CHECK(!strcmp(h, "da39a3ee5e6b4b0d3255bfef95601890afd80709"));
	sha1_hex(two, strlen(two), h);
	CHECK(!strcmp(h, "84983e441c3bd26ebaae4aa1f95129e5e54670f1"));
	memset(big, 'a', 1000000);
	sha1_hex(big, 1000000, h);
	CHECK(!strcmp(h, "34aa973cd4c4daa4f61eeb2bdbad27316534016f"));

	sha512_256_hex("abc", 3, h);
	CHECK(!strcmp(h, "53048e2681941ef99b2e29b76b4c7dabe4c2d0c634fc6d46e0e2f13107e7af23"));
	sha512_256_hex(two512, strlen(two512), h);
	CHECK(!strcmp(h, "3928e184fb8690f840da3988121d31be65cb9d3ef83ee6146feac861e19b563a"));
	sha512_256_hex("", 0, h);
	CHECK(!strcmp(h, "c672b8d1ef56ed28ab87c3622c5114069bdd3ad7b8f9737498d0c01ecef0967a"));
	free(big);
}

static void
json(void)
{
	const char *t = "{\"a\":[1,true,null,\"x\\u00e9\\n\"],\"b\":{\"c\":\"d\"},\"_SIGNATURE\":{}}";
	struct json *j = json_parse(t, strlen(t)), *a;

	CHECK(j && j->type == J_OBJ && j->n == 3);
	a = json_get(j, "a");
	CHECK(a && a->type == J_ARR && a->n == 4);
	CHECK(a && a->items[1]->type == J_BOOL && a->items[1]->boolean);
	CHECK(a && a->items[2]->type == J_NULL);
	CHECK(a && !strcmp(a->items[3]->str, "x\xc3\xa9\n"));
	CHECK(!strcmp(json_str(json_get(j, "b"), "c"), "d"));
	json_free(j);
	CHECK(!json_parse("{\"a\":}", 6));
	CHECK(!json_parse("[1,2", 4));
	CHECK(!json_parse("{} x", 4));
}

static void
actions(void)
{
	struct action a;

	CHECK(action_parse("file 33df0a path=usr/local/bin/hello mode=0555 "
	    "pkg.content-hash=file:sha512t_256:aa pkg.content-hash=gzip:sha512t_256:bb", &a) == 0);
	CHECK(!strcmp(a.type, "file") && !strcmp(a.payload, "33df0a"));
	CHECK(!strcmp(action_get(&a, "path"), "usr/local/bin/hello"));
	CHECK(!strcmp(content_hash(&a, "file"), "aa") && !strcmp(content_hash(&a, "gzip"), "bb"));
	action_free(&a);
	CHECK(action_parse("set name=pkg.summary value=\"Says \\\"hello\\\", louder\"", &a) == 0);
	CHECK(!a.payload && !strcmp(action_get(&a, "value"), "Says \"hello\", louder"));
	action_free(&a);
	CHECK(action_parse("depend fmri=library/world@1 type=require", &a) == 0);
	CHECK(!a.payload && !strcmp(action_get(&a, "fmri"), "library/world@1"));
	action_free(&a);
	CHECK(action_parse("set name=x value=\"unclosed", &a) < 0);
}

static void
versions(void)
{
	struct fmri f;

	CHECK(version_cmp("1.1,2.1-2:20261115T112620Z", "1.0,2.1-1:20261115T112616Z") > 0);
	CHECK(version_cmp("1.10,2.1-1", "1.9,2.1-5") > 0);
	CHECK(version_cmp("1.0,2.1-2", "1.0,2.1-10") < 0);
	CHECK(version_cmp("1.0,2.1-1:20261115T1", "1.0,2.1-1:20261115T2") < 0);
	CHECK(version_cmp("1.0,2.1-1", "1,2.1-1") == 0);
	CHECK(version_at_least("1.0,2.1-1", "1"));
	CHECK(!version_at_least("0.9,2.1-1", "1"));
	CHECK(version_at_least("6.3,2.1-14", "6.3"));
	CHECK(fmri_parse("pkg://amix/demo/hello@1.1,2.1-2:20261115T112620Z", &f) == 0);
	CHECK(!strcmp(f.publisher, "amix") && !strcmp(f.stem, "demo/hello") &&
	    !strcmp(f.version, "1.1,2.1-2:20261115T112620Z"));
	fmri_free(&f);
	CHECK(fmri_parse("pkg:/library/world", &f) == 0 && !f.version && !strcmp(f.stem, "library/world"));
	fmri_free(&f);
	{
		char *q = url_quote("1.0,2.1-1:2026"), *u = url_unquote(q);

		CHECK(!strcmp(q, "1.0%2C2.1-1%3A2026") && !strcmp(u, "1.0,2.1-1:2026"));
		free(q);
		free(u);
	}
}

int
main(void)
{
	hashes();
	json();
	actions();
	versions();
	if (fails)
		fprintf(stderr, "%d failed\n", fails);
	else
		printf("unit: all passed\n");
	return fails != 0;
}
