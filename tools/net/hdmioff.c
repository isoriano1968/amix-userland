/* hdmioff.c -- silence on the Z3660's HDMI: the sound DMA loops its ring for
 * ever, so what a stopped program left there keeps playing.
 *   hdmioff */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>

#define FB_PHYS   0x00200000UL
#define HDMI_OFF  0x07CE0000UL
#define HDMI_SIZE (3840UL * 8)

int main(void)
{
    int fd = open("/dev/zz9000", O_RDWR);
    char *p;

    if (fd < 0) {
        perror("/dev/zz9000");
        return 1;
    }
    p = (char *)mmap(0, 0x8000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)(HDMI_OFF - FB_PHYS));
    if (p == (char *)-1) {
        perror("mmap");
        return 1;
    }
    memset(p, 0, HDMI_SIZE);
    return 0;
}
