/*
 * setlogin.c -- write the login screen's screen mode for amixdisplay.
 * Installed setuid root as /opt/amix/lib/amixdisplay/setlogin, so every
 * user can choose it (the user's decision, 2026-11-07).
 *
 *   setlogin WxH [rate]
 *
 * It writes /etc/dt/config/rtg-mode and nothing else.  Xzz9000-dt passes the
 * whole of line 1 to the server's -mode, so line 1 is exactly "WxH"; line 2
 * is "rate N" when a rate is given.  Only the sizes of the Z3660 kernel
 * driver's table that Xzz9000 knows are accepted, and the rate must be
 * between 20 and 120 Hz.  The file is written next to the old one and
 * renamed over it, so a reader never sees half a file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#define FILE_   "/etc/dt/config/rtg-mode"
#define TMP     "/etc/dt/config/rtg-mode.new"

/* zz9k_modes_z3660 (amix-gate io/zz9000) that Xzz9000 has in its table:
   2560x1440 is not in it, 1920x800 is ZZ9000 only */
static const int sizes[][2] = {
    { 640, 400 }, { 640, 480 }, { 640, 512 }, { 720, 480 }, { 720, 576 },
    { 800, 600 }, { 1024, 768 }, { 1280, 720 }, { 1280, 800 }, { 1280, 1024 },
    { 1366, 768 }, { 1600, 900 }, { 1600, 1200 }, { 1680, 1050 }, { 1920, 1080 },
    { 1920, 1200 }
};

static int number(const char *s, const char **end)
{
    int n = 0, digits = 0;

    while (*s >= '0' && *s <= '9' && digits < 5) {
        n = n * 10 + (*s++ - '0');
        digits++;
    }
    *end = s;
    return digits ? n : -1;
}

int main(int argc, char **argv)
{
    const char *p;
    int w, h, rate = 0, i, fd, len;
    char buf[200];
    struct passwd *pw;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: setlogin WxH [rate]\n");
        return 2;
    }
    w = number(argv[1], &p);
    if (w < 0 || *p != 'x')
        goto bad;
    h = number(p + 1, &p);
    if (h < 0 || *p)
        goto bad;
    for (i = 0; i < (int)(sizeof sizes / sizeof sizes[0]); i++)
        if (sizes[i][0] == w && sizes[i][1] == h)
            break;
    if (i == (int)(sizeof sizes / sizeof sizes[0]))
        goto bad;
    if (argc == 3) {
        rate = number(argv[2], &p);
        if (rate < 0 || *p || (rate != 0 && (rate < 20 || rate > 120)))
            goto bad;
    }

    pw = getpwuid(getuid());
    len = sprintf(buf, "%dx%d\n", w, h);
    if (rate)
        len += sprintf(buf + len, "rate %d\n", rate);
    len += sprintf(buf + len, "# the login screen's mode, by amixdisplay (%.20s)\n",
                   pw ? pw->pw_name : "?");

    umask(022);
    unlink(TMP);
    fd = open(TMP, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        perror(TMP);
        return 1;
    }
    if (write(fd, buf, len) != len || fsync(fd) != 0 || close(fd) != 0) {
        perror(TMP);
        unlink(TMP);
        return 1;
    }
    if (rename(TMP, FILE_) != 0) {
        perror(FILE_);
        unlink(TMP);
        return 1;
    }
    return 0;
bad:
    fprintf(stderr, "setlogin: not a mode of the Z3660 X server: %s%s%s\n", argv[1],
            argc == 3 ? " " : "", argc == 3 ? argv[2] : "");
    return 2;
}
