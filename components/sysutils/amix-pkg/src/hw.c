/*
 * hw.c -- the boards this machine has, for org.amix.hw (amix-p5m's "hw"):
 *
 *   z3660         the Z3660's RTG: /dev/z3660rtg, else the Z3660 kernel's
 *                 /dev/zz9000 when the board behind it is a Z3660
 *   z3660-core1   programs on the Z3660's ARM core 1 (c1load, c1q3, c1uae,
 *                 zzgl): every Z3660
 *   zz9000        the MNT ZZ9000 on /dev/zz9000
 *
 * The drivers are asked, as Xamix asks them (RTGIOC_GETCAPS, else the
 * zz9000 driver's PROBE and GETINFO): a device node without its driver or
 * board (ENXIO, ENODEV) is no board.  PKG_HW overrides it all (tests, and a
 * disk prepared for another machine): tags separated by commas or blanks,
 * "none" for none.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "pkg.h"

/* amix-gate usr/src/uts/amiga/io/rtgcaps.h (RTGCAPS_VERSION 1) */
#define RTGIOC_GETCAPS		(('R' << 8) | 1)
#define RTGCAPS_VERSION		1
struct rtg_caps {
	unsigned short version, size;
	char board[24];
	unsigned long caps, depths, fb_size, max_width, max_height;
};

/* amix-gate io/zz9000's zz9000.h */
#define ZZ9KIOC_GETINFO		(('Z' << 8) | 1)
#define ZZ9KIOC_PROBE		(('Z' << 8) | 9)
#define ZZ9K_PRODUCT_Z3660	1
struct zz9k_info {
	unsigned long base, board_size, fb_offset, fb_size;
	unsigned short hw_version, fw_version, product, zorro, width, height, color_mode, mode;
};

static char found[64];
static int probed;
const char *hw_why = "";		/* for amix-hw -v: what the probe saw */

/* the board name RTGIOC_GETCAPS gives, "" if the driver does not answer;
 * -1: no driver or board behind the node */
static int
caps_board(const char *dev, char *board, size_t n)
{
	struct rtg_caps c;
	int fd = open(dev, O_RDONLY);

	board[0] = 0;
	if (fd < 0)
		return -1;
	memset(&c, 0, sizeof(c));
	c.version = RTGCAPS_VERSION;
	c.size = sizeof(c);
	if (ioctl(fd, RTGIOC_GETCAPS, (char *)&c) == 0) {
		c.board[sizeof(c.board) - 1] = 0;
		snprintf(board, n, "%s", c.board);
	} else if (errno == ENXIO || errno == ENODEV) {
		close(fd);
		return -1;
	} else if (strstr(dev, "zz9000")) {	/* an older zz9000 driver */
		struct zz9k_info info;

		memset(&info, 0, sizeof(info));
		if (ioctl(fd, ZZ9KIOC_PROBE, 0) < 0 || ioctl(fd, ZZ9KIOC_GETINFO, (char *)&info) < 0) {
			close(fd);
			return -1;
		}
		snprintf(board, n, "%s", info.product == ZZ9K_PRODUCT_Z3660 ? "Z3660" : "ZZ9000");
	}
	close(fd);
	return 0;
}

static void
add(const char *tag)
{
	if (found[0])
		strcat(found, " ");
	strcat(found, tag);
}

static void
probe(void)
{
	const char *e = getenv("PKG_HW");
	char board[32];

	if (probed)
		return;
	probed = 1;
	if (e && *e) {
		char *t, *dup = xstrdup(e);

		for (t = strtok(dup, ", \t"); t; t = strtok(NULL, ", \t"))
			if (strcmp(t, "none") && strlen(found) + strlen(t) + 2 < sizeof(found))
				add(t);
		free(dup);
		hw_why = "PKG_HW";
		return;
	}
	if (caps_board("/dev/z3660rtg", board, sizeof(board)) == 0 && !strncmp(board, "Z3660", 5)) {
		add("z3660");
		add("z3660-core1");
		hw_why = "/dev/z3660rtg";
	} else if (caps_board("/dev/zz9000", board, sizeof(board)) == 0) {
		if (!strncmp(board, "Z3660", 5)) {
			add("z3660");
			add("z3660-core1");
		} else
			add("zz9000");
		hw_why = "/dev/zz9000";
	} else
		hw_why = "no /dev/z3660rtg or /dev/zz9000 board";
}

/* the tags, blank-separated ("" for none) */
const char *
hw_list(void)
{
	probe();
	return found;
}

/* does the machine have one of want's alternatives (tag|tag...)? */
int
hw_has(const char *want)
{
	char *dup, *t;
	int ok = 0;

	probe();
	dup = xstrdup(want);
	for (t = strtok(dup, "|"); t && !ok; t = strtok(NULL, "|")) {
		const char *p = found;
		size_t l = strlen(t);

		while ((p = strstr(p, t)) != NULL) {
			if ((p == found || p[-1] == ' ') && (p[l] == 0 || p[l] == ' ')) {
				ok = 1;
				break;
			}
			p += l;
		}
	}
	free(dup);
	return ok;
}
