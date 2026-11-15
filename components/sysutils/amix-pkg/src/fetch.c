/* fetch.c -- files from a repository origin (http.c, or a folder); gzip payloads */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include "pkg.h"

/* relpath is below the repository root: publisher/amix/catalog/catalog.attrs */
int
fetch(const char *origin, const char *relpath, struct buf *out)
{
	const char *path = origin;
	char *full;
	int r;

	buf_add(out, "", 0);
	if (!strncmp(origin, "http://", 7)) {
		size_t l = strlen(origin);
		int status;

		struct buf q = { 0 };
		const char *p;

		/* the repository's file names hold %2F, %2C...: a web server decodes
		 * the URL once, so a literal % goes as %25 */
		for (p = relpath; *p; p++) {
			if (*p == '%')
				buf_add(&q, "%25", 3);
			else
				buf_add(&q, p, 1);
		}
		buf_add(&q, "", 1);
		full = xasprintf("%s%s%s", origin, l && origin[l - 1] == '/' ? "" : "/", q.data);
		buf_free(&q);
		status = http_request("GET", full, NULL, 0, out);
		if (status != 200) {
			if (status > 0 && verbose)
				warnx("%s: HTTP %d", full, status);
			if (status > 0)
				errno = status == 404 ? ENOENT : EIO;
			free(full);
			return -1;
		}
		free(full);
		return 0;
	}
	if (!strncmp(origin, "https://", 8)) {
		warnx("%s: no https: pkg speaks plain http (stock AMIX has no /dev/urandom "
		    "for TLS); every file is checked against its manifest hash", origin);
		errno = EINVAL;
		return -1;
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
