/* fetch.c -- files from a repository origin; gzip payloads */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>
#ifdef HAVE_CURL
#include <curl/curl.h>
#endif

#include "pkg.h"

#ifdef HAVE_CURL
static size_t
curl_put(void *p, size_t sz, size_t n, void *ctx)
{
	buf_add(ctx, p, sz * n);
	return sz * n;
}

static int
fetch_http(const char *url, struct buf *out)
{
	static int init;
	CURL *c;
	CURLcode rc;
	long code = 0;

	if (!init) {
		curl_global_init(CURL_GLOBAL_DEFAULT);
		init = 1;
	}
	if (!(c = curl_easy_init()))
		return -1;
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_put);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, out);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "amix-pkg");
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 30L);
	rc = curl_easy_perform(c);
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
	curl_easy_cleanup(c);
	if (rc != CURLE_OK) {
		if (verbose)
			warnx("%s: %s (%ld)", url, curl_easy_strerror(rc), code);
		errno = code == 404 ? ENOENT : EIO;
		return -1;
	}
	return 0;
}
#endif

/* relpath is below the repository root: publisher/amix/catalog/catalog.attrs */
int
fetch(const char *origin, const char *relpath, struct buf *out)
{
	const char *path = origin;
	char *full;
	int r;

	buf_add(out, "", 0);
	if (!strncmp(origin, "http://", 7) || !strncmp(origin, "https://", 8)) {
#ifdef HAVE_CURL
		size_t l = strlen(origin);

		full = xasprintf("%s%s%s", origin, l && origin[l - 1] == '/' ? "" : "/", relpath);
		r = fetch_http(full, out);
		free(full);
		return r;
#else
		warnx("%s: this build has no http (libcurl)", origin);
		errno = EINVAL;
		return -1;
#endif
	}
	if (!strncmp(origin, "file://", 7))
		path = origin + 7;
	full = root_path(path, relpath);
	r = read_file(full, out);
	free(full);
	return r;
}

int
gunzip(const struct buf *in, struct buf *out)
{
	z_stream z;
	unsigned char tmp[16384];
	int r;

	memset(&z, 0, sizeof(z));
	if (inflateInit2(&z, 16 + MAX_WBITS) != Z_OK)
		return -1;
	z.next_in = (unsigned char *)in->data;
	z.avail_in = (uInt)in->len;
	buf_add(out, "", 0);
	do {
		z.next_out = tmp;
		z.avail_out = sizeof(tmp);
		r = inflate(&z, Z_NO_FLUSH);
		if (r != Z_OK && r != Z_STREAM_END) {
			inflateEnd(&z);
			return -1;
		}
		buf_add(out, tmp, sizeof(tmp) - z.avail_out);
	} while (r != Z_STREAM_END);
	inflateEnd(&z);
	return 0;
}
