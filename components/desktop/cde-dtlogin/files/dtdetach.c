/*
 * dtdetach PROGRAM [ARG...] -- run PROGRAM in a session of its own, without
 * a controlling terminal, its standard files on /dev/null
 * (desktop/cde-dtlogin).  /etc/init.d/dtlogin starts dtlogin through it:
 * started from a login shell (telnet, the console), dtlogin went down with
 * that session's hangup although its -daemon calls setpgrp(), and AMIX's
 * at(1) cannot help (it refuses the year 2026).
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>

int main(int argc, char **argv)
{
    pid_t pid;
    int fd;

    if (argc < 2 || argv[1][0] != '/') {
        fprintf(stderr, "usage: dtdetach /path/to/program [arg...]\n");
        return 2;
    }
    pid = fork();
    if (pid < 0) {
        perror("dtdetach: fork");
        return 1;
    }
    if (pid > 0)
        return 0;               /* the caller's shell goes on */
    if (setsid() < 0)           /* a new session: no terminal, no hangup */
        _exit(1);
    signal(SIGHUP, SIG_IGN);    /* until the program sets its own */
    if ((fd = open("/dev/null", O_RDWR)) >= 0) {
        dup2(fd, 0);
        dup2(fd, 1);
        if (fd > 1)
            close(fd);
    }
    execv(argv[1], argv + 1);
    _exit(127);
}
