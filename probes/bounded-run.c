/* alarm survives exec: bound an unmodified application experiment. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    int seconds;
    if (argc < 3 || (seconds = atoi(argv[1])) < 1 || seconds > 120) {
        fputs("usage: bounded-run seconds command [arguments]\n", stderr);
        return 2;
    }
    alarm(seconds);
    execvp(argv[2], argv + 2);
    perror("execvp");
    return 127;
}
