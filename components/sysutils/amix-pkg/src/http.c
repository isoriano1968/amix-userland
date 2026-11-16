/*
 * http.c -- plain HTTP/1.0 over a socket: GET for the repository, PUT for the
 * installer's reports. No TLS on purpose: stock AMIX has no /dev/urandom, so
 * OpenSSL (and libcurl with it) cannot even start there. Every payload is
 * checked against the hash in its manifest anyway.
 */
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "pkg.h"

#ifdef __amix__
/* AMIX's SVR4 headers declare the types but not these (libsocket has them) */
int socket(int, int, int);
int connect(int, const struct sockaddr *, int);
int send(int, const void *, int, int);
int recv(int, void *, int, int);
#endif

#define TIMEOUT		120	/* seconds without a byte: give up */
#define REDIRECTS	5

/* http://host[:port]/path -> its parts; 0 or -1 */
int
http_split(const char *url, char *host, size_t hlen, int *port, const char **path)
{
	const char *h, *e, *c;
	size_t n;

	if (strncmp(url, "http://", 7))
		return -1;
	h = url + 7;
	e = strchr(h, '/');
	if (!e)
		e = h + strlen(h);
	c = memchr(h, ':', (size_t)(e - h));
	n = (size_t)((c ? c : e) - h);
	if (n == 0 || n >= hlen)
		return -1;
	memcpy(host, h, n);
	host[n] = 0;
	*port = 80;
	if (c) {
		char *end;
		long p = strtol(c + 1, &end, 10);

		if (end != e || p <= 0 || p > 65535)
			return -1;
		*port = (int)p;
	}
	*path = *e ? e : "/";
	return 0;
}

static void
on_alarm(int sig)
{
	(void)sig;			/* only to make a blocked recv return EINTR */
}

static int
dial(const char *host, int port)
{
	struct sockaddr_in sa;
	uint32_t a;
	int s;

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short)port);
	a = (uint32_t)inet_addr(host);		/* AMIX: unsigned long, no in_addr_t */
	memcpy(&sa.sin_addr, &a, sizeof(a));
	if (a == 0xffffffffu) {
		struct hostent *he = gethostbyname(host);

		if (!he || he->h_addrtype != AF_INET) {
			warnx("%s: unknown host", host);
			errno = ENOENT;
			return -1;
		}
		memcpy(&sa.sin_addr, he->h_addr_list[0], sizeof(sa.sin_addr));
	}
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return -1;
	/* blocking: AMIX's non-blocking connect reports success too early */
	if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		int e = errno;

		close(s);
		errno = e;
		return -1;
	}
	return s;
}

static int
send_all(int s, const char *p, size_t n)
{
	while (n > 0) {
		ssize_t w = send(s, p, n, 0);

		if (w < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		p += w;
		n -= (size_t)w;
	}
	return 0;
}

#define HEAD_MAX	65536	/* status line and headers: no more */

/*
 * one request: the status line and headers into head (a C string, ending
 * with the blank line), the body to sink in pieces as it comes; with
 * drop_body (a redirect) the body is read and dropped. 0, or -1 with errno.
 */
static int
exchange(const char *method, const char *host, int port, const char *path,
    const void *body, size_t blen, struct buf *head, http_sink sink, void *arg)
{
	char tmp[8192];
	char *req;
	int s, r = 0, in_body = 0;
	void (*old)(int);

	if ((s = dial(host, port)) < 0)
		return -1;
	req = xasprintf("%s %s HTTP/1.0\r\nHost: %s:%d\r\nUser-Agent: amix-pkg\r\n"
	    "Connection: close\r\nContent-Length: %lu\r\n\r\n",
	    method, path, host, port, (unsigned long)blen);
	if (send_all(s, req, strlen(req)) < 0 || (blen && send_all(s, body, blen) < 0))
		r = -1;
	free(req);
	buf_add(head, "", 0);
	old = signal(SIGALRM, on_alarm);
	while (r == 0) {
		ssize_t n;
		const char *e;

		alarm(TIMEOUT);
		n = recv(s, tmp, sizeof(tmp), 0);
		alarm(0);
		if (n < 0) {
			if (errno == EINTR)
				errno = ETIMEDOUT;
			r = -1;
			break;
		}
		if (n == 0)
			break;
		if (in_body) {
			if (sink && sink(tmp, (size_t)n, arg) < 0)
				r = -1;
			continue;
		}
		buf_add(head, tmp, (size_t)n);
		if ((e = strstr(head->data, "\r\n\r\n")) != NULL) {
			size_t hl = (size_t)(e - head->data) + 4, rest = head->len - hl;

			in_body = 1;
			sink = (head->len >= 12 && head->data[9] == '3') ? NULL : sink;
			if (rest && sink && sink(head->data + hl, rest, arg) < 0)
				r = -1;
			head->len = hl;
			head->data[hl] = 0;
		} else if (head->len > HEAD_MAX) {
			errno = EPROTO;
			r = -1;
		}
	}
	signal(SIGALRM, old);
	close(s);
	if (r == 0 && !in_body) {
		errno = EPROTO;		/* no blank line after the headers */
		r = -1;
	}
	return r;
}

/* the value of header name (lower case) in head, or NULL; into val */
static const char *
header(const char *head, const char *name, char *val, size_t vlen)
{
	size_t nl = strlen(name);
	const char *l = strstr(head, "\r\n");

	while (l && l[2] && l[2] != '\r') {
		const char *p = l + 2, *e = strstr(p, "\r\n");
		size_t i;

		if (!e)
			break;
		for (i = 0; i < nl && p[i] && (p[i] | 0x20) == name[i]; i++)
			;
		if (i == nl && p[nl] == ':') {
			p += nl + 1;
			while (*p == ' ' || *p == '\t')
				p++;
			i = (size_t)(e - p);
			if (i >= vlen)
				i = vlen - 1;
			memcpy(val, p, i);
			val[i] = 0;
			return val;
		}
		l = e;
	}
	return NULL;
}

/*
 * method GET or PUT on url; the body of the answer to sink, in pieces.
 * Returns the HTTP status (redirects followed), or -1 with errno when there
 * was no answer (or the sink failed).
 */
int
http_stream(const char *method, const char *url, const void *body, size_t blen,
    http_sink sink, void *arg)
{
	char host[256], loc[1024];
	char *cur = xstrdup(url);
	int hop, status = -1;

	for (hop = 0; hop <= REDIRECTS; hop++) {
		struct buf head = { 0 };
		const char *path;
		int port;

		if (http_split(cur, host, sizeof(host), &port, &path) < 0) {
			warnx("%s: not an http:// URL (no https here)", cur);
			errno = EINVAL;
			break;
		}
		if (exchange(method, host, port, path, body, blen, &head, sink, arg) < 0) {
			if (errno == EPROTO)
				warnx("%s: not an HTTP answer", cur);
			buf_free(&head);
			break;
		}
		if (strncmp(head.data, "HTTP/1.", 7) || head.len < 12) {
			warnx("%s: not an HTTP answer", cur);
			buf_free(&head);
			errno = EPROTO;
			break;
		}
		status = atoi(head.data + 9);
		if ((status == 301 || status == 302 || status == 303 || status == 307 ||
		    status == 308) && header(head.data, "location", loc, sizeof(loc)) &&
		    !strncmp(loc, "http://", 7)) {
			buf_free(&head);
			free(cur);
			cur = xstrdup(loc);
			status = -1;
			continue;
		}
		buf_free(&head);
		break;
	}
	free(cur);
	return status;
}

static int
to_buf(const void *p, size_t n, void *arg)
{
	buf_add(arg, p, n);
	return 0;
}

/* as http_stream, the body into out */
int
http_request(const char *method, const char *url, const void *body, size_t blen,
    struct buf *out)
{
	buf_add(out, "", 0);
	out->len = 0;
	return http_stream(method, url, body, blen, to_buf, out);
}
