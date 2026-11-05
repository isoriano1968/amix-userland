/*
 * dlna.c -- the DLNA media servers for amixcast, through libupnp: an SSDP
 * search for MediaServer devices, each one's description for its name and
 * ContentDirectory control URL, and Browse for a folder's entries.
 *
 * Compiled with GNU Pth's pthread.h (pth.pc's pth_cflags): libupnp's threads
 * are Pth's and run only while this thread waits in a Pth call, so the search
 * waits with Pth's sleep().  Browse is synchronous in the caller's thread.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <upnp.h>
#include <upnptools.h>
#include <UpnpDiscovery.h>
#include "dlna.h"

/* entries asked for at a time: small XML pieces for the 68060, well within BROWSE_MAX */
#define BROWSE_PAGE	"50"
#define BROWSE_MAX	(4 * 1024 * 1024)

#define MAXSERVER 16
#define CDS "urn:schemas-upnp-org:service:ContentDirectory:1"

static struct server {
	char location[512];
	char name[128];
	char control[512];
} server[MAXSERVER];
static int nserver, started;
static UpnpClient_Handle handle;

/* the search results, from libupnp's thread: one entry per description */
static int found(Upnp_EventType t, void *ev, void *cookie)
{
	const char *loc;
	int i;

	(void)cookie;
	if (t != UPNP_DISCOVERY_SEARCH_RESULT && t != UPNP_DISCOVERY_ADVERTISEMENT_ALIVE)
		return 0;
	loc = UpnpDiscovery_get_Location_cstr((const UpnpDiscovery *)ev);
	for (i = 0; i < nserver; i++)
		if (strcmp(server[i].location, loc) == 0)
			return 0;
	if (nserver < MAXSERVER)
		snprintf(server[nserver++].location, sizeof server[0].location, "%s", loc);
	return 0;
}

/* UTF-8 to Latin-1, which Motif's lists show here; '?' beyond it */
static void latin1(char *dst, size_t len, const char *src)
{
	const unsigned char *s = (const unsigned char *)src;
	size_t o = 0;

	while (*s && o + 1 < len) {
		unsigned int c = *s++;

		if (c >= 0xc0 && c < 0xe0 && (*s & 0xc0) == 0x80)
			c = ((c & 0x1f) << 6) | (*s++ & 0x3f);
		else if (c >= 0x80) {
			while ((*s & 0xc0) == 0x80)
				s++;
			c = '?';
		}
		dst[o++] = c > 0xff ? '?' : (char)c;
	}
	dst[o] = 0;
}

/* the text of the first element called name below n, or NULL */
static const char *text(IXML_Node *n, const char *name)
{
	IXML_NodeList *l = ixmlElement_getElementsByTagName((IXML_Element *)n, name);
	const char *v = NULL;

	if (l) {
		IXML_Node *t = ixmlNode_getFirstChild(ixmlNodeList_item(l, 0));

		if (t)
			v = ixmlNode_getNodeValue(t);
		ixmlNodeList_free(l);
	}
	return v;
}

/* the name and ContentDirectory control URL from a server's description */
static int describe(struct server *s)
{
	IXML_Document *d = NULL;
	IXML_NodeList *svc;
	const char *n;
	unsigned long k;

	if (UpnpDownloadXmlDoc(s->location, &d) != UPNP_E_SUCCESS)
		return -1;
	n = text((IXML_Node *)d, "friendlyName");
	latin1(s->name, sizeof s->name, n ? n : s->location);
	s->control[0] = 0;
	svc = ixmlDocument_getElementsByTagName(d, "service");
	for (k = 0; svc && k < ixmlNodeList_length(svc); k++) {
		IXML_Node *e = ixmlNodeList_item(svc, k);
		const char *type = text(e, "serviceType"), *url = text(e, "controlURL");

		if (type && url && strstr(type, "ContentDirectory"))
			UpnpResolveURL(s->location, url, s->control);
	}
	if (svc)
		ixmlNodeList_free(svc);
	ixmlDocument_free(d);
	return s->control[0] ? 0 : -1;
}

int dlna_search(int secs)
{
	int i, n;

	if (!started) {
		if (UpnpInit2(NULL, 0) != UPNP_E_SUCCESS ||
		    UpnpRegisterClient(found, NULL, &handle) != UPNP_E_SUCCESS)
			return -1;
		/* libupnp drops answers over 64000 bytes (chunked ones too): a
		   page of Windows Media sharing's Browse, every entry with its
		   resources, thumbnails and metadata in escaped XML, is far more.
		   The servers here are our own LAN's. */
		UpnpSetMaxContentLength(BROWSE_MAX);
		started = 1;
	}
	nserver = 0;
	UpnpSearchAsync(handle, secs > 1 ? secs - 1 : 1, "urn:schemas-upnp-org:device:MediaServer:1", NULL);
	sleep(secs);			/* Pth's: libupnp's threads take the answers */
	/* keep the ones that describe themselves with a ContentDirectory */
	for (i = n = 0; i < nserver; i++)
		if (describe(&server[i]) == 0)
			server[n++] = server[i];
	nserver = n;
	return n;
}

const char *dlna_name(int s)
{
	return s >= 0 && s < nserver ? server[s].name : "?";
}

/* "H:MM:SS(.fff)" to seconds */
static int seconds(const char *d)
{
	int h = 0, m = 0, s = 0;

	if (!d || sscanf(d, "%d:%d:%d", &h, &m, &s) != 3)
		return 0;
	return h * 3600 + m * 60 + s;
}

static char kind(const char *protocol_info)
{
	if (!protocol_info)
		return '?';
	if (strstr(protocol_info, ":video/"))
		return 'v';
	if (strstr(protocol_info, ":audio/"))
		return 'a';
	if (strstr(protocol_info, ":image/"))
		return 'i';
	return '?';
}

/* the entries of one DIDL-Lite result */
static int entries(const char *didl, dlna_entry_fn fn, void *ctx)
{
	IXML_Document *d = ixmlParseBuffer(didl);
	IXML_Node *root, *e;
	int n = 0;

	if (!d)
		return -1;
	root = ixmlNode_getFirstChild((IXML_Node *)d);
	for (e = root ? ixmlNode_getFirstChild(root) : NULL; e; e = ixmlNode_getNextSibling(e)) {
		const char *tag = ixmlNode_getNodeName(e), *id, *title;
		char t[400];

		if (!tag || ixmlNode_getNodeType(e) != eELEMENT_NODE)
			continue;
		id = ixmlElement_getAttribute((IXML_Element *)e, "id");
		title = text(e, "dc:title");
		latin1(t, sizeof t, title ? title : "?");
		if (!id)
			continue;
		if (strcmp(tag, "container") == 0) {
			fn(ctx, 'd', id, t, 0, NULL);
			n++;
		} else if (strcmp(tag, "item") == 0) {
			IXML_NodeList *res = ixmlElement_getElementsByTagName((IXML_Element *)e, "res");
			IXML_Node *r = res ? ixmlNodeList_item(res, 0) : NULL, *u = r ? ixmlNode_getFirstChild(r) : NULL;
			const char *url = u ? ixmlNode_getNodeValue(u) : NULL;

			if (url) {
				fn(ctx, kind(ixmlElement_getAttribute((IXML_Element *)r, "protocolInfo")), id, t,
				   seconds(ixmlElement_getAttribute((IXML_Element *)r, "duration")), url);
				n++;
			}
			if (res)
				ixmlNodeList_free(res);
		}
	}
	ixmlDocument_free(d);
	return n;
}

int dlna_browse(int s, const char *id, dlna_entry_fn fn, void *ctx)
{
	int start = 0, total = 0;

	if (s < 0 || s >= nserver)
		return -1;
	for (;;) {
		IXML_Document *act, *resp = NULL;
		const char *result, *returned, *matches;
		char from[16];
		int n, m;

		snprintf(from, sizeof from, "%d", start);
		act = UpnpMakeAction("Browse", CDS, 6, "ObjectID", id, "BrowseFlag", "BrowseDirectChildren",
				     "Filter", "*", "StartingIndex", from, "RequestedCount", BROWSE_PAGE, "SortCriteria", "");
		if (!act)
			return -1;
		if (UpnpSendAction(handle, server[s].control, CDS, NULL, act, &resp) != UPNP_E_SUCCESS) {
			ixmlDocument_free(act);
			return total ? total : -1;
		}
		ixmlDocument_free(act);
		result = text((IXML_Node *)resp, "Result");
		returned = text((IXML_Node *)resp, "NumberReturned");
		matches = text((IXML_Node *)resp, "TotalMatches");
		n = result ? entries(result, fn, ctx) : 0;
		m = returned ? atoi(returned) : 0;
		total += n > 0 ? n : 0;
		start += m;
		if (!m || !matches || start >= atoi(matches)) {
			ixmlDocument_free(resp);
			return total;
		}
		ixmlDocument_free(resp);
	}
}
