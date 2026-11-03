/*
 * dlna.h -- the DLNA media servers on the network, for amixcast: found with
 * libupnp (an SSDP search, broadcast on AMIX), browsed with ContentDirectory.
 * dlna.c is compiled with GNU Pth's pthread.h, amixcast.c is not, so that
 * amixcast's own sockets keep the plain socket calls.
 */
#ifndef DLNA_H
#define DLNA_H

/* one entry of a folder: kind 'd' a folder, 'v' video, 'a' audio, 'i'
   image, '?' other; url the item's resource (NULL for a folder) */
typedef void (*dlna_entry_fn)(void *ctx, char kind, const char *id,
                              const char *title, int secs, const char *url);

/* look for servers for about secs seconds; the number found, -1 if libupnp
   does not start.  Again: a fresh list */
int dlna_search(int secs);
const char *dlna_name(int server);
/* the folder id (top: "0") of a server, entry by entry; the number of
   entries, -1 on failure */
int dlna_browse(int server, const char *id, dlna_entry_fn fn, void *ctx);

#endif
