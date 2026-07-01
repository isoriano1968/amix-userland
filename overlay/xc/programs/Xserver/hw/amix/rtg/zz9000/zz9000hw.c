/*
** zz9000hw.c - MNT ZZ9000 hardware init/close for xamix-rtg.
*/

#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include "../../amix.h"
#include "../rtg.h"
#include "zz9000.h"

static ZZ9000ModeRec zz9000_modes[] = {
    { "640x480",   640,  480 },
    { "800x600",   800,  600 },
    { "1024x768", 1024,  768 },
    { "1280x720", 1280,  720 },
    { "1280x1024",1280, 1024 },
    { "1920x800", 1920,  800 },
    { (char *)0 }
};

ZZ9000ModePtr ZZ9000_selected_mode = &zz9000_modes[5];

Bool
ZZ9000SetMode(name)
char *name;
{
    ZZ9000ModePtr m;

    for (m = zz9000_modes; m->name; m++) {
        if (strcmp(m->name, name) == 0) {
            ZZ9000_selected_mode = m;
            return TRUE;
        }
    }
    return FALSE;
}

Bool
ZZ9000Probe()
{
    int fd;

    fd = open(ZZ9000_DEV, O_RDWR);
    if (fd < 0)
        return FALSE;
    if (ioctl(fd, ZZ9KIOC_PROBE, 0) < 0) {
        (void) close(fd);
        return FALSE;
    }
    (void) close(fd);
    return TRUE;
}

Bool
ZZ9000InitHW(pRTG)
rtgScreenPtr pRTG;
{
    ZZ9000ScreenPtr pZZ;
    struct zz9k_mode mode;
    struct zz9k_info info;
    struct zz9k_fbinfo fbi;
    struct zz9k_fill fill;
    caddr_t base;
    int fd;

    ErrorF("ZZ9000InitHW: starting\n");

    pZZ = (ZZ9000ScreenPtr) xalloc(sizeof(ZZ9000ScreenRec));
    if (!pZZ) {
        ErrorF("ZZ9000InitHW: xalloc failed\n");
        return FALSE;
    }
    memset((char *)pZZ, 0, sizeof(ZZ9000ScreenRec));

    fd = open(ZZ9000_DEV, O_RDWR);
    if (fd < 0) {
        ErrorF("ZZ9000InitHW: cannot open %s (%s)\n",
               ZZ9000_DEV, strerror(errno));
        xfree((pointer)pZZ);
        return FALSE;
    }

    if (ioctl(fd, ZZ9KIOC_PROBE, 0) < 0) {
        ErrorF("ZZ9000InitHW: probe failed (%s)\n", strerror(errno));
        (void) close(fd);
        xfree((pointer)pZZ);
        return FALSE;
    }

    mode.width = (unsigned short)ZZ9000_selected_mode->w;
    mode.height = (unsigned short)ZZ9000_selected_mode->h;
    mode.color_mode = ZZ9K_COLOR_16BIT565;
    mode.scale_mode = 0;

    if (ioctl(fd, ZZ9KIOC_SETMODE, &mode) < 0) {
        ErrorF("ZZ9000InitHW: SETMODE %dx%d failed (%s)\n",
               ZZ9000_selected_mode->w, ZZ9000_selected_mode->h,
               strerror(errno));
        (void) close(fd);
        xfree((pointer)pZZ);
        return FALSE;
    }

    if (ioctl(fd, ZZ9KIOC_GETINFO, &info) < 0 ||
        ioctl(fd, ZZ9KIOC_GETFBINFO, &fbi) < 0) {
        ErrorF("ZZ9000InitHW: GETINFO failed (%s)\n", strerror(errno));
        (void) close(fd);
        xfree((pointer)pZZ);
        return FALSE;
    }

    base = (caddr_t)mmap(0, info.board_size,
                         PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == (caddr_t)-1) {
        ErrorF("ZZ9000InitHW: mmap failed (%s)\n", strerror(errno));
        (void) close(fd);
        xfree((pointer)pZZ);
        return FALSE;
    }

    fill.offset = 0;
    fill.bytes = (unsigned long)fbi.height * fbi.stride;
    fill.value = 0;
    (void) ioctl(fd, ZZ9KIOC_FILL, &fill);

    if (ioctl(fd, ZZ9KIOC_SETSWITCH, ZZ9K_SWITCH_RTG) < 0)
        ErrorF("ZZ9000InitHW: SETSWITCH RTG failed (%s)\n", strerror(errno));

    pZZ->fd       = fd;
    pZZ->mapBase  = (pointer)base;
    pZZ->mapSize  = info.board_size;
    pZZ->fbOffset = fbi.fb_offset;
    pZZ->fbSize   = fbi.fb_size;
    pZZ->fbBase   = (unsigned short *)(base + fbi.fb_offset);

    pRTG->width        = fbi.width;
    pRTG->height       = fbi.height;
    pRTG->pitch        = fbi.stride;
    pRTG->bitsPerPixel = ZZ9000_BPP;
    pRTG->depth        = ZZ9000_DEPTH;
    pRTG->fbOffset     = fbi.fb_offset;
    pRTG->frameBase    = (pointer)base;
    pRTG->fbBase       = pZZ->fbBase;
    pRTG->cardPrivate  = (pointer)pZZ;

    ErrorF("ZZ9000InitHW: done %dx%d stride %d\n",
           pRTG->width, pRTG->height, pRTG->pitch);
    return TRUE;
}

void
ZZ9000CloseHW(pRTG)
rtgScreenPtr pRTG;
{
    ZZ9000ScreenPtr pZZ = GetZZ9000Screen(pRTG);

    if (!pZZ)
        return;

    if (pZZ->fd >= 0)
        (void) ioctl(pZZ->fd, ZZ9KIOC_SETSWITCH, ZZ9K_SWITCH_CAPTURE);
    if (pZZ->mapBase)
        (void) munmap(pZZ->mapBase, pZZ->mapSize);
    if (pZZ->fd >= 0)
        (void) close(pZZ->fd);

    xfree((pointer)pZZ);
    pRTG->cardPrivate = (pointer)NULL;
    pRTG->frameBase = (pointer)NULL;
    pRTG->fbBase = (unsigned short *)NULL;
}
