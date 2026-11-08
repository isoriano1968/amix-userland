/*
 * amixprfind.c -- find the network printers: one broadcast search over UPnP
 * (SSDP) and Bonjour (mDNS), then a line per printer on stdout.
 *
 *   amixprfind [-t seconds]
 *
 * AMIX (SVR4.0) has no IP multicast, so both questions go out as broadcasts
 * to 255.255.255.255 from an ephemeral port; a responder then answers by
 * unicast (mDNS "legacy unicast", RFC 6762 6.7; SSDP to the sender's port).
 *
 * Output, one line per printer, fields separated by tabs:
 *
 *   ip  port  proto  name  model  pdl  admin
 *
 *   proto   raw (port 9100 style, _pdl-datastream), ipp, lpd, upnp
 *   pdl     the page description languages the printer names (Bonjour
 *           TXT pdl=), "-" when it does not say
 *
 * A printer that answers over several protocols gives several lines; the
 * front end (amixprinters) groups them by address.  Only libc and the socket
 * library: no X, so it can run from a shell too.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXP 32
#define S 128

struct printer {
	char ip[16];		/* the A record or the SSDP sender */
	int port;
	char proto[8];
	char name[S];		/* service instance or friendlyName */
	char host[S];		/* SRV target, to match the A record */
	char model[S];
	char pdl[256];
	char admin[S];
	char loc[S];		/* SSDP LOCATION */
};

static struct printer pr[MAXP];
static int npr;

/* ---------- mDNS ---------- */

static const char *svcs[][2] = {
	{ "_pdl-datastream._tcp.local", "raw" },
	{ "_ipp._tcp.local", "ipp" },
	{ "_printer._tcp.local", "lpd" },
};
#define NSVC (int)(sizeof svcs / sizeof svcs[0])

static int put_name(unsigned char *p, const char *name)
{
	int n = 0;

	while (*name) {
		const char *dot = strchr(name, '.');
		int l = dot ? (int)(dot - name) : (int)strlen(name);

		p[n++] = l;
		memcpy(p + n, name, l);
		n += l;
		name += l;
		if (*name == '.')
			name++;
	}
	p[n++] = 0;
	return n;
}

static int mdns_query(unsigned char *p)
{
	int n = 12, i;

	memset(p, 0, 12);
	p[0] = 0x41; p[1] = 0x4d;		/* id: legacy unicast wants one */
	p[5] = NSVC;				/* QDCOUNT */
	for (i = 0; i < NSVC; i++) {
		n += put_name(p + n, svcs[i][0]);
		p[n++] = 0; p[n++] = 12;	/* PTR */
		p[n++] = 0; p[n++] = 1;		/* IN */
	}
	return n;
}

/* a (compressed) name at p[off] into out as "a.b.c"; returns the offset
   after it in the record, -1 on a bad packet */
static int get_name(const unsigned char *p, int len, int off, char *out, int outlen)
{
	int o = off, end = -1, jumps = 0, n = 0;

	out[0] = 0;
	while (o < len) {
		int l = p[o];

		if (l == 0) {
			if (end < 0)
				end = o + 1;
			if (n > 0 && out[n - 1] == '.')
				out[--n] = 0;
			return end;
		}
		if ((l & 0xc0) == 0xc0) {
			if (o + 1 >= len || ++jumps > 20)
				return -1;
			if (end < 0)
				end = o + 2;
			o = ((l & 0x3f) << 8) | p[o + 1];
			continue;
		}
		if (o + 1 + l > len)
			return -1;
		if (n + l + 2 < outlen) {
			memcpy(out + n, p + o + 1, l);
			n += l;
			out[n++] = '.';
			out[n] = 0;
		}
		o += 1 + l;
	}
	return -1;
}

static struct printer *by_name(const char *name, const char *proto)
{
	int i;

	for (i = 0; i < npr; i++)
		if (!strcmp(pr[i].name, name) && !strcmp(pr[i].proto, proto))
			return &pr[i];
	if (npr == MAXP)
		return NULL;
	memset(&pr[npr], 0, sizeof pr[npr]);
	strncpy(pr[npr].name, name, S - 1);
	strcpy(pr[npr].proto, proto);
	return &pr[npr++];
}

/* "Xerox WorkCentre 6515 (c9:b9:b3)._pdl-datastream._tcp.local" -> its
   service's proto; the instance keeps its full name as the key */
static const char *svc_proto(const char *full)
{
	int i, fl = strlen(full);

	for (i = 0; i < NSVC; i++) {
		int sl = strlen(svcs[i][0]);

		if (fl > sl && full[fl - sl - 1] == '.' && !strcasecmp(full + fl - sl, svcs[i][0]))
			return svcs[i][1];
	}
	return NULL;
}

static void txt(struct printer *q, const unsigned char *d, int dl)
{
	int o = 0;

	while (o < dl) {
		int l = d[o++];
		char kv[300], *eq;

		if (o + l > dl)
			break;
		if (l < (int)sizeof kv) {
			memcpy(kv, d + o, l);
			kv[l] = 0;
			eq = strchr(kv, '=');
			if (eq) {
				*eq++ = 0;
				if (!strcasecmp(kv, "ty"))
					strncpy(q->model, eq, S - 1);
				else if (!strcasecmp(kv, "product") && !q->model[0]) {
					strncpy(q->model, eq + (*eq == '('), S - 1);
					if (q->model[0] && q->model[strlen(q->model) - 1] == ')')
						q->model[strlen(q->model) - 1] = 0;
				} else if (!strcasecmp(kv, "pdl"))
					strncpy(q->pdl, eq, sizeof q->pdl - 1);
				else if (!strcasecmp(kv, "adminurl"))
					strncpy(q->admin, eq, S - 1);
			}
		}
		o += l;
	}
}

static void mdns_answer(const unsigned char *p, int len, const char *from)
{
	int qd, an, i, o = 12;
	char name[S], tgt[S];

	if (len < 12 || !(p[2] & 0x80))
		return;				/* not a response */
	qd = p[4] << 8 | p[5];
	an = (p[6] << 8 | p[7]) + (p[8] << 8 | p[9]) + (p[10] << 8 | p[11]);
	for (i = 0; i < qd; i++) {
		if ((o = get_name(p, len, o, name, S)) < 0)
			return;
		o += 4;
	}
	for (i = 0; i < an && o + 10 <= len; i++) {
		int type, dl, d;
		struct printer *q;
		const char *proto;

		if ((o = get_name(p, len, o, name, S)) < 0 || o + 10 > len)
			return;
		type = p[o] << 8 | p[o + 1];
		dl = p[o + 8] << 8 | p[o + 9];
		d = o + 10;
		if (d + dl > len)
			return;
		o = d + dl;
		switch (type) {
		case 12:			/* PTR: service -> instance */
			if (get_name(p, len, d, tgt, S) < 0 || !(proto = svc_proto(tgt)))
				break;
			if ((q = by_name(tgt, proto)) && !q->ip[0])
				strncpy(q->ip, from, 15);
			break;
		case 33:			/* SRV: port, host */
			if (!(proto = svc_proto(name)) || dl < 7 || !(q = by_name(name, proto)))
				break;
			q->port = p[d + 4] << 8 | p[d + 5];
			get_name(p, len, d + 6, q->host, S);
			break;
		case 16:			/* TXT */
			if ((proto = svc_proto(name)) && (q = by_name(name, proto)))
				txt(q, p + d, dl);
			break;
		case 1:				/* A: host -> address */
			if (dl == 4) {
				int k;
				char ip[16];

				sprintf(ip, "%d.%d.%d.%d", p[d], p[d + 1], p[d + 2], p[d + 3]);
				for (k = 0; k < npr; k++)
					if (!strcasecmp(pr[k].host, name))
						strcpy(pr[k].ip, ip);
			}
			break;
		}
	}
}

/* ---------- SSDP ---------- */

static const char ssdp_msg[] =
	"M-SEARCH * HTTP/1.1\r\n"
	"HOST: 239.255.255.250:1900\r\n"
	"MAN: \"ssdp:discover\"\r\n"
	"MX: 2\r\n"
	"ST: urn:schemas-upnp-org:device:Printer:1\r\n\r\n";

static const char *ci_find(const char *h, const char *n)
{
	int nl = strlen(n);

	for (; *h; h++)
		if (!strncasecmp(h, n, nl))
			return h;
	return NULL;
}

static void header(const char *msg, const char *key, char *out, int outlen)
{
	const char *h = ci_find(msg, key);
	int n = 0;

	out[0] = 0;
	if (!h)
		return;
	h += strlen(key);
	while (*h == ' ' || *h == ':')
		h++;
	while (*h && *h != '\r' && *h != '\n' && n < outlen - 1)
		out[n++] = *h++;
	out[n] = 0;
}

static void ssdp_answer(const char *msg, const char *from)
{
	char st[S], loc[S];
	struct printer *q;

	header(msg, "\nST", st, S);
	header(msg, "\nLOCATION", loc, S);
	if (!ci_find(st, "printer") || !loc[0])
		return;
	if (!(q = by_name(loc, "upnp")))
		return;
	strncpy(q->ip, from, 15);
	strncpy(q->loc, loc, S - 1);
	q->port = 9100;				/* UPnP names no data port: raw */
}

/* GET the device description; friendlyName and modelName from it */
static void xml_tag(const char *x, const char *tag, char *out, int outlen)
{
	char open[64];
	const char *s, *e;

	sprintf(open, "<%s>", tag);
	if (!out[0] && (s = strstr(x, open)) && (e = strstr(s, "</"))) {
		s += strlen(open);
		if (e - s < outlen) {
			memcpy(out, s, e - s);
			out[e - s] = 0;
		}
	}
}

static void ssdp_describe(struct printer *q)
{
	char host[64], path[S], req[300], buf[8192];
	int port = 80, s, n = 0, r;
	const char *u = q->loc + 7, *slash, *colon;
	struct sockaddr_in a;

	if (strncmp(q->loc, "http://", 7))
		return;
	slash = strchr(u, '/');
	if (!slash)
		slash = u + strlen(u);
	colon = memchr(u, ':', slash - u);
	if ((colon ? colon : slash) - u >= (int)sizeof host)
		return;
	memcpy(host, u, (colon ? colon : slash) - u);
	host[(colon ? colon : slash) - u] = 0;
	if (colon)
		port = atoi(colon + 1);
	strncpy(path, *slash ? slash : "/", S - 1);
	path[S - 1] = 0;
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_port = htons(port);
	a.sin_addr.s_addr = inet_addr(host);
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return;
	alarm(5);				/* a blocking connect: SVR4.0's sockmod */
	if (connect(s, (struct sockaddr *)&a, sizeof a) == 0) {
		sprintf(req, "GET %s HTTP/1.0\r\nHost: %s:%d\r\n\r\n", path, host, port);
		if (write(s, req, strlen(req)) > 0)
			while (n < (int)sizeof buf - 1 && (r = read(s, buf + n, sizeof buf - 1 - n)) > 0)
				n += r;
	}
	alarm(0);
	close(s);
	buf[n] = 0;
	xml_tag(buf, "friendlyName", q->name, S);
	if (!q->name[0])
		strcpy(q->name, host);
	q->model[0] = 0;
	xml_tag(buf, "modelName", q->model, S);
}

static void on_alarm(int sig)
{
	(void)sig;
}

/* ---------- main ---------- */

static void field(const char *s)
{
	for (; *s; s++)
		putchar(*s == '\t' || *s == '\n' ? ' ' : *s);
}

int main(int argc, char **argv)
{
	int secs = 3, sm, ss, on = 1, i, len;
	unsigned char q[512], buf[9000];
	struct sockaddr_in a, from;
	time_t t0;

	if (argc == 3 && !strcmp(argv[1], "-t"))
		secs = atoi(argv[2]);
	else if (argc != 1) {
		fprintf(stderr, "usage: amixprfind [-t seconds]\n");
		return 2;
	}
	if (secs < 1 || secs > 30)
		secs = 3;
	signal(SIGALRM, on_alarm);

	sm = socket(AF_INET, SOCK_DGRAM, 0);
	ss = socket(AF_INET, SOCK_DGRAM, 0);
	if (sm < 0 || ss < 0) {
		perror("amixprfind: socket");
		return 1;
	}
	setsockopt(sm, SOL_SOCKET, SO_BROADCAST, (char *)&on, sizeof on);
	setsockopt(ss, SOL_SOCKET, SO_BROADCAST, (char *)&on, sizeof on);
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_BROADCAST);

	len = mdns_query(q);
	for (i = 0; i < 2; i++) {		/* twice: one UDP packet can get lost */
		a.sin_port = htons(5353);
		if (sendto(sm, (char *)q, len, 0, (struct sockaddr *)&a, sizeof a) < 0)
			perror("amixprfind: mdns sendto");
		a.sin_port = htons(1900);
		if (sendto(ss, ssdp_msg, sizeof ssdp_msg - 1, 0, (struct sockaddr *)&a, sizeof a) < 0)
			perror("amixprfind: ssdp sendto");
	}

	t0 = time(NULL);
	while (time(NULL) - t0 < secs) {
		fd_set r;
		struct timeval tv;
		int fl = sizeof from, n, mx = sm > ss ? sm : ss;

		FD_ZERO(&r);
		FD_SET(sm, &r);
		FD_SET(ss, &r);
		tv.tv_sec = 0;
		tv.tv_usec = 250000;
		if (select(mx + 1, &r, NULL, NULL, &tv) <= 0)
			continue;
		if (FD_ISSET(sm, &r) &&
		    (n = recvfrom(sm, (char *)buf, sizeof buf, 0, (struct sockaddr *)&from, &fl)) > 0)
			mdns_answer(buf, n, inet_ntoa(from.sin_addr));
		fl = sizeof from;
		if (FD_ISSET(ss, &r) &&
		    (n = recvfrom(ss, (char *)buf, sizeof buf - 1, 0, (struct sockaddr *)&from, &fl)) > 0) {
			buf[n] = 0;
			ssdp_answer((char *)buf, inet_ntoa(from.sin_addr));
		}
	}

	for (i = 0; i < npr; i++) {
		struct printer *p = &pr[i];
		char *paren;

		if (!strcmp(p->proto, "upnp"))
			ssdp_describe(p);
		else if ((paren = strstr(p->name, "._")))	/* the instance's label */
			*paren = 0;
		if (!p->ip[0] || !p->port)
			continue;
		printf("%s\t%d\t%s\t", p->ip, p->port, p->proto);
		field(p->name[0] ? p->name : p->ip);
		putchar('\t');
		field(p->model[0] ? p->model : "-");
		putchar('\t');
		field(p->pdl[0] ? p->pdl : "-");
		putchar('\t');
		field(p->admin[0] ? p->admin : "-");
		putchar('\n');
	}
	return 0;
}
