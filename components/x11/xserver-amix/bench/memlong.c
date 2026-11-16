/* memlong.c -- read, or write and read back, one long in /dev/mem (root).
 * (2026-10-26)  For the 68060 CACR experiment: the kernel reloads CACR from
 * its .data longs `cacr` (user mode, at ttrap) and `sup_cacr` (every
 * exception entry), so changing those words changes the live CACR at the
 * next interrupt.  Address = 0x08000000 + .text size + the symbol's .data
 * offset of the booted image.
 *
 *   memlong ADDR            print the long at ADDR (hex)
 *   memlong ADDR VALUE      write VALUE, then print what reads back
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    unsigned long addr, v;
    int fd;

    if (argc < 2) {
        fprintf(stderr, "usage: memlong ADDR [VALUE]\n");
        return 2;
    }
    addr = strtoul(argv[1], 0, 16);
    fd = open("/dev/mem", argc > 2 ? O_RDWR : O_RDONLY);
    if (fd < 0) { perror("/dev/mem"); return 1; }
    if (argc > 2) {
        v = strtoul(argv[2], 0, 16);
        if (lseek(fd, (long) addr, 0) < 0 || write(fd, &v, 4) != 4) { perror("write"); return 1; }
    }
    if (lseek(fd, (long) addr, 0) < 0 || read(fd, &v, 4) != 4) { perror("read"); return 1; }
    printf("%08lx: %08lx\n", addr, v);
    return 0;
}
