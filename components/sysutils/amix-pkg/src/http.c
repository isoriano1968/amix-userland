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

/* one request; the whole response (status line, headers, body) into raw */
static int
exchange(const char *method, const char *host, int port, const char *path,
    const void *body, size_t blen, struct buf *raw)
{
	char tmp[8192];
	char *req;
	int s, r = 0;
	void (*old)(int);

	if ((s = dial(host, port)) < 0)
		return -1;
	req = xasprintf("%s %s HTTP/1.0\r\nHost: %s:%d\r\nUser-Agent: amix-pkg\r\n"
	    "Connection: close\r\nContent-Length: %lu\r\n\r\n",
	    method, path, host, port, (unsigned long)blen);
	if (send_all(s, req, strlen(req)) < 0 || (blen && send_all(s, body, blen) < 0))
		r = -1;
	free(req);
	old = signal(SIGALRM, on_alarm);
	while (r == 0) {
		ssize_t n;

		alarm(TIMEOUT);
		n = recv(s, tmp, sizeof(tmp), 0);
		alarm(0);
		if (n < 0) {
			if (errno == EINTR)
				errno = ETIMEDOUT;
			r = -1;
		} else if (n == 0)
			break;
		else
			buf_add(raw, tmp, (size_t)n);
	}
	signal(SIGALRM, old);
	close(s);
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
 * method GET or PUT on url; the body of the answer into out. Returns the HTTP
 * status (redirects followed), or -1 with errno when there was no answer.
 */
int
http_request(const char *method, const char *url, const void *body, size_t blen,
    struct buf *out)
{
	char host[256], loc[1024];
	char *cur = xstrdup(url);
	int hop, status = -1;

	buf_add(out, "", 0);
	for (hop = 0; hop <= REDIRECTS; hop++) {
		struct buf raw = { 0 };
		const char *path, *end;
		char *head;
		int port;

		if (http_split(cur, host, sizeof(host), &port, &path) < 0) {
			warnx("%s: not an http:// URL (no https here)", cur);
			errno = EINVAL;
			break;
		}
		if (exchange(method, host, port, path, body, blen, &raw) < 0) {
			buf_free(&raw);
			break;
		}
		buf_add(&raw, "", 1);		/* NUL for the header scan */
		raw.len--;
		end = strstr(raw.data, "\r\n\r\n");
		if (strncmp(raw.data, "HTTP/1.", 7) || !end || strlen(raw.data) < 12) {
			warnx("%s: not an HTTP answer", cur);
			buf_free(&raw);
			errno = EPROTO;
			break;
		}
		status = atoi(raw.data + 9);
		head = xmalloc((size_t)(end - raw.data) + 3);
		memcpy(head, raw.data, (size_t)(end - raw.data) + 2);
		head[(end - raw.data) + 2] = 0;
		if ((status == 301 || status == 302 || status == 303 || status == 307 ||
		    status == 308) && header(head, "location", loc, sizeof(loc)) &&
		    !strncmp(loc, "http://", 7)) {
			free(head);
			buf_free(&raw);
			free(cur);
			cur = xstrdup(loc);
			status = -1;
			continue;
		}
		free(head);
		end += 4;
		out->len = 0;
		buf_add(out, end, raw.len - (size_t)(end - raw.data));
		buf_free(&raw);
		break;
	}
	free(cur);
	return status;
}
